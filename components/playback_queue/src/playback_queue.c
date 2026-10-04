#include "playback_queue.h"

#include <string.h>

#include "audio_pipeline.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_FEATURE_AUDIO_OUTPUT && __has_include("audio_output.h")
#include "audio_output.h"
#define PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT 1
#else
#define PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT 0
#endif
#if CONFIG_FEATURE_VOLUME_CONTROL && __has_include("volume_control.h")
#include "volume_control.h"
#define PLAYBACK_QUEUE_HAS_VOLUME_CONTROL 1
#else
#define PLAYBACK_QUEUE_HAS_VOLUME_CONTROL 0
#endif

static const char *const TAG = "playback_queue";

#define PLAYBACK_QUEUE_CAPACITY CONFIG_PLAYBACK_QUEUE_CAPACITY

// Pending items are referenced by pointer, so the fixed array only stores a few
// addresses and the 20 KB frame payload of each item lives in PSRAM. That keeps
// the internal heap free for Wi-Fi, TLS, and the audio DMA descriptors.
static playback_queue_item_t *playback_queue_pending[PLAYBACK_QUEUE_CAPACITY];
static size_t playback_queue_pending_count;
static playback_queue_item_t *playback_queue_active;
static playback_queue_snapshot_t playback_queue_snapshot;
static SemaphoreHandle_t playback_queue_mutex;
static SemaphoreHandle_t playback_queue_signal;
static SemaphoreHandle_t playback_queue_task_exit;
static TaskHandle_t playback_queue_worker;
static volatile bool playback_queue_ready;
static volatile bool playback_queue_running;
static bool playback_queue_is_paused;

/** Reason the worker must stop before finishing the active item. */
typedef enum {
    PLAYBACK_STOP_NONE = 0,
    PLAYBACK_STOP_PREEMPT,
    PLAYBACK_STOP_DISCARD,
} playback_stop_reason_t;

// A higher priority item (PREEMPT) returns the unplayed tail to the pending set
// so an interrupted reply finishes later. An explicit clear (DISCARD) drops the
// tail. The worker re-reads the reason before every frame under the mutex.
static playback_stop_reason_t playback_queue_stop_reason = PLAYBACK_STOP_NONE;

static playback_queue_item_t *playback_queue_allocate_item(void) {
    const size_t item_size = sizeof(playback_queue_item_t);
    playback_queue_item_t *item = heap_caps_malloc_prefer(
        item_size,
        2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_8BIT
    );
    if (item == NULL) {
        return NULL;
    }
    memset(item, 0, item_size);
    return item;
}

static void playback_queue_free_item(playback_queue_item_t *item) {
    if (item != NULL) {
        free(item);
    }
}

static void playback_queue_signal_worker(void) {
    if (playback_queue_signal != NULL) {
        xSemaphoreGive(playback_queue_signal);
    }
}

// insert_locked keeps the pending set sorted by descending priority. Items of
// equal priority keep their arrival order so audio of one class stays coherent.
static void playback_queue_insert_locked(playback_queue_item_t *item) {
    size_t insert_index = playback_queue_pending_count;
    for (size_t index = 0; index < playback_queue_pending_count; ++index) {
        if (item->priority > playback_queue_pending[index]->priority) {
            insert_index = index;
            break;
        }
    }
    for (size_t index = playback_queue_pending_count; index > insert_index;
         --index) {
        playback_queue_pending[index] = playback_queue_pending[index - 1];
    }
    playback_queue_pending[insert_index] = item;
    ++playback_queue_pending_count;
}

static bool playback_queue_contains_id_locked(const char *item_id) {
    for (size_t index = 0; index < playback_queue_pending_count; ++index) {
        if (strcmp(playback_queue_pending[index]->item_id, item_id) == 0) {
            return true;
        }
    }
    if (playback_queue_active != NULL &&
        strcmp(playback_queue_active->item_id, item_id) == 0) {
        return true;
    }
    return false;
}

#if PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT
/**
 * @brief Map the queue policy class to the matching mixer lane.
 *
 * The enums intentionally use the same order, but the conversion is explicit
 * so a future addition to either contract fails at compile time instead of
 * silently changing the speaking lane.
 */
static audio_output_priority_t playback_queue_output_priority(
    playback_priority_t priority
) {
    switch (priority) {
        case PLAYBACK_PRIORITY_SAFETY:
            return AUDIO_OUTPUT_PRIORITY_SAFETY;
        case PLAYBACK_PRIORITY_PROMPT:
            return AUDIO_OUTPUT_PRIORITY_PROMPT;
        case PLAYBACK_PRIORITY_CONVERSATION:
            return AUDIO_OUTPUT_PRIORITY_CONVERSATION;
        case PLAYBACK_PRIORITY_AMBIENT:
        default:
            return AUDIO_OUTPUT_PRIORITY_AMBIENT;
    }
}

static void playback_queue_discard_output_priority(
    playback_priority_t priority
) {
    (void)audio_output_discard_priority(
        playback_queue_output_priority(priority),
        NULL
    );
}
#endif

// requeue_remaining_locked returns the unplayed tail of an interrupted item to
// the pending set. The copy keeps the original identifier, which is safe
// because the interrupted item has already left the pending set.
static void playback_queue_requeue_remaining_locked(
    const playback_queue_item_t *item,
    size_t next_frame_index
) {
    if (item == NULL || next_frame_index >= item->frame_count) {
        return;
    }
    if (playback_queue_pending_count >= PLAYBACK_QUEUE_CAPACITY) {
        ++playback_queue_snapshot.dropped_items;
        return;
    }

    playback_queue_item_t *resumed = playback_queue_allocate_item();
    if (resumed == NULL) {
        ++playback_queue_snapshot.dropped_items;
        return;
    }
    memcpy(resumed, item, offsetof(playback_queue_item_t, frames));
    resumed->frame_count = item->frame_count - next_frame_index;
    memcpy(
        resumed->frames,
        &item->frames[next_frame_index],
        resumed->frame_count * sizeof(audio_codec_pcm_frame_t)
    );
    playback_queue_insert_locked(resumed);
}

static void playback_queue_drop_pending_suppressible_locked(bool include_safety) {
    size_t retained = 0;
    for (size_t index = 0; index < playback_queue_pending_count; ++index) {
        playback_queue_item_t *item = playback_queue_pending[index];
        const bool is_safety = item->priority == PLAYBACK_PRIORITY_SAFETY;
        if (is_safety && !include_safety) {
            playback_queue_pending[retained++] = item;
            continue;
        }
        playback_queue_free_item(item);
    }
    playback_queue_pending_count = retained;
}

/**
 * @brief Play the active item and report why it stopped.
 *
 * Returns the number of frames written to the speaker. The caller decides
 * whether the unplayed tail is resumed or discarded based on the stop reason,
 * which keeps preemption and mute behaviour separate from audio I/O.
 */
static size_t playback_queue_play_active_item(void) {
#if !PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT
    // Playback must be enabled explicitly; a muted or disabled speaker is a
    // normal state, so a failure here only drops the item.
    if (audio_pipeline_set_playing(true) != ESP_OK) {
        ESP_LOGW(TAG, "speaker enable failed; dropping item");
        return 0;
    }
#endif

    size_t frames_played = 0;
    for (; frames_played < playback_queue_active->frame_count;
         ++frames_played) {
        playback_stop_reason_t stop_reason = PLAYBACK_STOP_NONE;
        if (xSemaphoreTake(playback_queue_mutex, portMAX_DELAY) == pdTRUE) {
            stop_reason = playback_queue_stop_reason;
            xSemaphoreGive(playback_queue_mutex);
        }
        if (stop_reason != PLAYBACK_STOP_NONE) {
            break;
        }

#if !PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT && PLAYBACK_QUEUE_HAS_VOLUME_CONTROL
        if (volume_control_is_muted() &&
            playback_queue_active->priority != PLAYBACK_PRIORITY_SAFETY) {
            if (xSemaphoreTake(playback_queue_mutex, portMAX_DELAY) == pdTRUE) {
                playback_queue_stop_reason = PLAYBACK_STOP_DISCARD;
                xSemaphoreGive(playback_queue_mutex);
            }
            break;
        }
#endif

        // The stored frame stays unscaled so a resumed item is scaled exactly
        // once by the output stage or fallback volume that is active when it
        // finally plays.
        audio_codec_pcm_frame_t frame =
            playback_queue_active->frames[frames_played];
#if PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT
        const audio_output_error_t output_result =
            audio_output_submit_blocking(
                &frame,
                playback_queue_output_priority(
                    playback_queue_active->priority
                )
            );
        if (output_result != AUDIO_OUTPUT_OK) {
            ESP_LOGW(
                TAG,
                "mixer frame failed: %s",
                audio_output_error_name(output_result)
            );
            break;
        }
#else
#if PLAYBACK_QUEUE_HAS_VOLUME_CONTROL
        volume_control_apply_gain(
            frame.pcm,
            frame.pcm_size / sizeof(int16_t)
        );
#endif
        const audio_codec_error_t result = audio_pipeline_play_frame(&frame);
        if (result != AUDIO_CODEC_OK) {
            ESP_LOGW(
                TAG,
                "playback frame failed: %s",
                audio_codec_error_name(result)
            );
            break;
        }
#endif
    }
    return frames_played;
}

/**
 * @brief Retire the active item and apply its stop reason.
 *
 * Must be called with the mutex held. Returns true when the worker should
 * continue with the next pending item without waiting for a new signal.
 */
static bool playback_queue_retire_active_locked(size_t frames_played) {
    const playback_stop_reason_t stop_reason = playback_queue_stop_reason;
    playback_queue_stop_reason = PLAYBACK_STOP_NONE;

    if (stop_reason == PLAYBACK_STOP_PREEMPT) {
#if PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT
        playback_queue_discard_output_priority(
            playback_queue_active->priority
        );
#endif
        playback_queue_requeue_remaining_locked(
            playback_queue_active,
            frames_played
        );
    } else if (stop_reason == PLAYBACK_STOP_DISCARD &&
               frames_played < playback_queue_active->frame_count) {
        playback_queue_snapshot.dropped_items +=
            playback_queue_active->frame_count - frames_played;
    }

    playback_queue_free_item(playback_queue_active);
    playback_queue_active = NULL;
    playback_queue_snapshot.has_active_item = false;
    playback_queue_snapshot.active_priority = PLAYBACK_PRIORITY_AMBIENT;
    playback_queue_snapshot.pending_items = playback_queue_pending_count;
    playback_queue_snapshot.is_paused = playback_queue_is_paused;
    return playback_queue_pending_count > 0 && !playback_queue_is_paused;
}

static void playback_queue_worker_main(void *context) {
    (void)context;

    while (playback_queue_running) {
        if (xSemaphoreTake(playback_queue_signal, pdMS_TO_TICKS(100)) !=
            pdTRUE) {
            continue;
        }

        while (playback_queue_running) {
            if (xSemaphoreTake(playback_queue_mutex, portMAX_DELAY) != pdTRUE) {
                break;
            }
            if (playback_queue_is_paused ||
                playback_queue_pending_count == 0) {
                xSemaphoreGive(playback_queue_mutex);
                break;
            }

            playback_queue_active = playback_queue_pending[0];
            for (size_t index = 1; index < playback_queue_pending_count;
                 ++index) {
                playback_queue_pending[index - 1] =
                    playback_queue_pending[index];
            }
            --playback_queue_pending_count;
            playback_queue_pending[playback_queue_pending_count] = NULL;

            playback_queue_snapshot.has_active_item = true;
            playback_queue_snapshot.active_priority =
                playback_queue_active->priority;
            playback_queue_stop_reason = PLAYBACK_STOP_NONE;
            xSemaphoreGive(playback_queue_mutex);

            const size_t frames_played = playback_queue_play_active_item();

            xSemaphoreTake(playback_queue_mutex, portMAX_DELAY);
            const bool has_more =
                playback_queue_retire_active_locked(frames_played);
            xSemaphoreGive(playback_queue_mutex);

            if (!has_more) {
                break;
            }
        }
    }

    if (playback_queue_task_exit != NULL) {
        xSemaphoreGive(playback_queue_task_exit);
    }
    playback_queue_worker = NULL;
    vTaskDelete(NULL);
}

esp_err_t playback_queue_init(void) {
    if (playback_queue_ready) {
        return ESP_OK;
    }
    if (!audio_pipeline_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_FEATURE_AUDIO_OUTPUT
    if (!audio_output_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
#endif

    playback_queue_mutex = xSemaphoreCreateMutex();
    playback_queue_signal = xSemaphoreCreateBinary();
    playback_queue_task_exit = xSemaphoreCreateBinary();
    if (playback_queue_mutex == NULL || playback_queue_signal == NULL ||
        playback_queue_task_exit == NULL) {
        return ESP_ERR_NO_MEM;
    }

    playback_queue_running = true;
    const BaseType_t created = xTaskCreate(
        playback_queue_worker_main,
        "audio_playback",
        CONFIG_PLAYBACK_QUEUE_TASK_STACK_SIZE,
        NULL,
        CONFIG_PLAYBACK_QUEUE_TASK_PRIORITY,
        &playback_queue_worker
    );
    if (created != pdPASS) {
        playback_queue_running = false;
        return ESP_ERR_NO_MEM;
    }

    playback_queue_snapshot.dropped_items = 0;
    playback_queue_ready = true;
    return ESP_OK;
}

bool playback_queue_is_ready(void) {
    return playback_queue_ready;
}

void playback_queue_shutdown(void) {
    if (!playback_queue_ready && playback_queue_worker == NULL) {
        return;
    }

    playback_queue_ready = false;
    playback_queue_running = false;
    playback_queue_is_paused = false;
    playback_queue_signal_worker();

    if (playback_queue_worker != NULL) {
        if (xSemaphoreTake(
                playback_queue_task_exit,
                pdMS_TO_TICKS(5000)
            ) != pdTRUE) {
            // Keep the mutex and queue storage alive if the worker is still
            // unwinding. A later shutdown call can retry once it exits.
            ESP_LOGE(TAG, "playback worker did not stop; keeping resources");
            return;
        }
        playback_queue_worker = NULL;
    }
    if (playback_queue_mutex != NULL) {
        (void)xSemaphoreTake(playback_queue_mutex, portMAX_DELAY);
        playback_queue_drop_pending_suppressible_locked(true);
        playback_queue_free_item(playback_queue_active);
        playback_queue_active = NULL;
        xSemaphoreGive(playback_queue_mutex);
        vSemaphoreDelete(playback_queue_mutex);
        playback_queue_mutex = NULL;
    }
    if (playback_queue_signal != NULL) {
        vSemaphoreDelete(playback_queue_signal);
        playback_queue_signal = NULL;
    }
    if (playback_queue_task_exit != NULL) {
        vSemaphoreDelete(playback_queue_task_exit);
        playback_queue_task_exit = NULL;
    }
    playback_queue_snapshot = (playback_queue_snapshot_t){0};
}

playback_queue_error_t playback_queue_enqueue(
    const playback_queue_item_t *item
) {
    if (!playback_queue_ready) {
        return PLAYBACK_QUEUE_ERR_NOT_INITIALIZED;
    }
    if (item == NULL || item->item_id[0] == '\0' ||
        item->frame_count == 0 ||
        item->frame_count > PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM) {
        return PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < item->frame_count; ++index) {
        if (item->frames[index].pcm_size !=
            AUDIO_CODEC_FRAME_PCM_BYTES) {
            return PLAYBACK_QUEUE_ERR_EMPTY_PAYLOAD;
        }
    }

    playback_queue_item_t *queued = playback_queue_allocate_item();
    if (queued == NULL) {
        return PLAYBACK_QUEUE_ERR_FULL;
    }
    memcpy(queued, item, sizeof(playback_queue_item_t));
    // Safety audio must reach the child, so the queue refuses to mark it
    // interruptible no matter what the caller requested.
    if (queued->priority == PLAYBACK_PRIORITY_SAFETY) {
        queued->is_interruptible = false;
    }

    if (xSemaphoreTake(playback_queue_mutex, portMAX_DELAY) != pdTRUE) {
        playback_queue_free_item(queued);
        return PLAYBACK_QUEUE_ERR_NOT_INITIALIZED;
    }

    if (playback_queue_contains_id_locked(queued->item_id)) {
        xSemaphoreGive(playback_queue_mutex);
        playback_queue_free_item(queued);
        return PLAYBACK_QUEUE_ERR_DUPLICATE_ITEM;
    }

    if (playback_queue_active != NULL &&
        queued->priority > playback_queue_active->priority) {
        if (!playback_queue_active->is_interruptible) {
            xSemaphoreGive(playback_queue_mutex);
            playback_queue_free_item(queued);
            return PLAYBACK_QUEUE_ERR_INTERRUPT_DENIED;
        }
        playback_queue_stop_reason = PLAYBACK_STOP_PREEMPT;
    }

    if (playback_queue_pending_count >= PLAYBACK_QUEUE_CAPACITY) {
        xSemaphoreGive(playback_queue_mutex);
        playback_queue_free_item(queued);
        return PLAYBACK_QUEUE_ERR_FULL;
    }

    playback_queue_insert_locked(queued);
    playback_queue_snapshot.pending_items = playback_queue_pending_count;
    xSemaphoreGive(playback_queue_mutex);

    playback_queue_signal_worker();
    return PLAYBACK_QUEUE_OK;
}

esp_err_t playback_queue_pause(void) {
    if (!playback_queue_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    playback_queue_is_paused = true;
    return ESP_OK;
}

esp_err_t playback_queue_resume(void) {
    if (!playback_queue_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    playback_queue_is_paused = false;
    playback_queue_signal_worker();
    return ESP_OK;
}

esp_err_t playback_queue_clear(bool include_safety) {
    if (!playback_queue_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(playback_queue_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    playback_queue_drop_pending_suppressible_locked(include_safety);

    // A generic clear stops conversational audio that is already playing, but
    // a safety announcement keeps running to completion.
    if (playback_queue_active != NULL &&
        (include_safety ||
         playback_queue_active->priority != PLAYBACK_PRIORITY_SAFETY)) {
        playback_queue_stop_reason = PLAYBACK_STOP_DISCARD;
#if PLAYBACK_QUEUE_HAS_AUDIO_OUTPUT
        playback_queue_discard_output_priority(
            playback_queue_active->priority
        );
#endif
    }
    playback_queue_snapshot.pending_items = playback_queue_pending_count;
    xSemaphoreGive(playback_queue_mutex);
    return ESP_OK;
}

playback_queue_snapshot_t playback_queue_get_snapshot(void) {
    playback_queue_snapshot_t snapshot;
    if (playback_queue_mutex == NULL) {
        return playback_queue_snapshot;
    }
    if (xSemaphoreTake(playback_queue_mutex, portMAX_DELAY) != pdTRUE) {
        return playback_queue_snapshot;
    }
    playback_queue_snapshot.pending_items = playback_queue_pending_count;
    playback_queue_snapshot.is_paused = playback_queue_is_paused;
    snapshot = playback_queue_snapshot;
    xSemaphoreGive(playback_queue_mutex);
    return snapshot;
}

const char *playback_priority_name(playback_priority_t priority) {
    switch (priority) {
        case PLAYBACK_PRIORITY_SAFETY:
            return "safety";
        case PLAYBACK_PRIORITY_PROMPT:
            return "prompt";
        case PLAYBACK_PRIORITY_CONVERSATION:
            return "conversation";
        case PLAYBACK_PRIORITY_AMBIENT:
            return "ambient";
        default:
            return "ambient";
    }
}

const char *playback_queue_error_name(playback_queue_error_t error) {
    switch (error) {
        case PLAYBACK_QUEUE_OK:
            return "ok";
        case PLAYBACK_QUEUE_ERR_NOT_INITIALIZED:
            return "audio_codec_not_initialized";
        case PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT:
            return "audio_codec_invalid_argument";
        case PLAYBACK_QUEUE_ERR_FULL:
            return "audio_queue_full";
        case PLAYBACK_QUEUE_ERR_DUPLICATE_ITEM:
            return "audio_queue_duplicate_item";
        case PLAYBACK_QUEUE_ERR_EMPTY_PAYLOAD:
            return "audio_queue_payload_empty";
        case PLAYBACK_QUEUE_ERR_INTERRUPT_DENIED:
            return "audio_queue_interrupt_denied";
        case PLAYBACK_QUEUE_ERR_NOT_FOUND:
        default:
            return "audio_codec_internal";
    }
}

const module_descriptor_t *playback_queue_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "playback_queue",
        .version = "1.0.0",
        .initialize = playback_queue_init,
        .shutdown = playback_queue_shutdown,
    };
    return &descriptor;
}
