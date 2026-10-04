#include "audio_output.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_pipeline.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_FEATURE_VOLUME_CONTROL && __has_include("volume_control.h")
#include "volume_control.h"
#define AUDIO_OUTPUT_HAS_VOLUME_CONTROL 1
#else
#define AUDIO_OUTPUT_HAS_VOLUME_CONTROL 0
#endif

static const char *const TAG = "audio_output";

#define AUDIO_OUTPUT_QUEUE_CAPACITY CONFIG_AUDIO_OUTPUT_QUEUE_FRAMES

typedef struct {
    uint64_t sequence;
    audio_codec_pcm_frame_t *frames;
    size_t head;
    size_t tail;
    size_t count;
    uint64_t completed_sequence;
    uint64_t discarded_through_sequence;
} audio_output_lane_t;

static audio_output_lane_t audio_output_lanes[AUDIO_OUTPUT_PRIORITY_COUNT];
static audio_output_snapshot_t audio_output_snapshot;
static SemaphoreHandle_t audio_output_mutex;
static SemaphoreHandle_t audio_output_signal;
static SemaphoreHandle_t audio_output_task_exit;
static TaskHandle_t audio_output_task_handle;
static volatile bool audio_output_running;
static bool audio_output_ready;

static bool audio_output_priority_is_valid(audio_output_priority_t priority) {
    return priority >= AUDIO_OUTPUT_PRIORITY_AMBIENT &&
        priority < AUDIO_OUTPUT_PRIORITY_COUNT;
}

static void audio_output_signal_task(void) {
    if (audio_output_signal != NULL) {
        xSemaphoreGive(audio_output_signal);
    }
}

static void audio_output_recompute_primary_locked(void) {
    audio_output_snapshot.has_primary_source =
        audio_output_lanes[AUDIO_OUTPUT_PRIORITY_AMBIENT].count > 0 ||
        audio_output_lanes[AUDIO_OUTPUT_PRIORITY_CONVERSATION].count > 0;
}

static audio_output_error_t audio_output_queue_push_locked(
    audio_output_priority_t priority,
    const audio_codec_pcm_frame_t *frame,
    uint64_t *sequence_out
) {
    audio_output_lane_t *lane = &audio_output_lanes[priority];
    if (lane->count >= AUDIO_OUTPUT_QUEUE_CAPACITY) {
        return AUDIO_OUTPUT_ERR_QUEUE_FULL;
    }

    lane->sequence++;
    lane->frames[lane->tail] = *frame;
    lane->tail = (lane->tail + 1) % AUDIO_OUTPUT_QUEUE_CAPACITY;
    lane->count++;
    audio_output_snapshot.submitted_frames++;
    audio_output_recompute_primary_locked();
    if (sequence_out != NULL) {
        *sequence_out = lane->sequence;
    }
    return AUDIO_OUTPUT_OK;
}

static bool audio_output_queue_pop_locked(
    audio_output_priority_t priority,
    audio_codec_pcm_frame_t *frame_out
) {
    audio_output_lane_t *lane = &audio_output_lanes[priority];
    if (lane->count == 0) {
        return false;
    }

    *frame_out = lane->frames[lane->head];
    lane->head = (lane->head + 1) % AUDIO_OUTPUT_QUEUE_CAPACITY;
    lane->count--;
    audio_output_recompute_primary_locked();
    return true;
}

static size_t audio_output_queue_discard_locked(
    audio_output_priority_t priority
) {
    audio_output_lane_t *lane = &audio_output_lanes[priority];
    const size_t discarded = lane->count;
    if (discarded > 0) {
        // Wake any blocking submitter for a frame that is still queued. The
        // next submit uses a strictly newer sequence, so it remains playable.
        lane->discarded_through_sequence = lane->sequence;
    }
    lane->head = 0;
    lane->tail = 0;
    lane->count = 0;
    return discarded;
}

static void audio_output_count_discarded_locked(size_t discarded) {
    audio_output_snapshot.discarded_frames += (uint32_t)discarded;
    audio_output_recompute_primary_locked();
}

static int32_t audio_output_scale_sample(int16_t sample, uint8_t percent) {
    const int32_t scaled =
        ((int32_t)sample * (int32_t)percent) / 100;
    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return scaled;
}

static int16_t audio_output_saturate_add(int32_t left, int32_t right) {
    const int32_t sum = left + right;
    if (sum > INT16_MAX) {
        return INT16_MAX;
    }
    if (sum < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sum;
}

static uint8_t audio_output_normal_volume_percent(void) {
#if AUDIO_OUTPUT_HAS_VOLUME_CONTROL
    if (volume_control_is_ready()) {
        return volume_control_get_percent();
    }
#endif
    return (uint8_t)CONFIG_AUDIO_OUTPUT_FALLBACK_VOLUME_PERCENT;
}

static uint8_t audio_output_safety_volume_percent(void) {
    uint8_t maximum = (uint8_t)CONFIG_AUDIO_OUTPUT_FALLBACK_VOLUME_PERCENT;

#if AUDIO_OUTPUT_HAS_VOLUME_CONTROL
    if (volume_control_is_ready()) {
        maximum = volume_control_get_max_percent();
    }
#endif

    if (maximum < CONFIG_AUDIO_OUTPUT_SAFETY_MIN_PERCENT) {
        maximum = (uint8_t)CONFIG_AUDIO_OUTPUT_SAFETY_MIN_PERCENT;
    }
    return maximum;
}

static void audio_output_render_frame(
    const bool has_source[AUDIO_OUTPUT_PRIORITY_COUNT],
    const audio_codec_pcm_frame_t source[AUDIO_OUTPUT_PRIORITY_COUNT],
    audio_codec_pcm_frame_t *mixed_out,
    uint8_t *applied_volume_percent_out
) {
    const uint8_t normal_volume = audio_output_normal_volume_percent();
    const uint8_t safety_volume = audio_output_safety_volume_percent();

    mixed_out->sequence = 0;
    mixed_out->pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    memset(mixed_out->pcm, 0, sizeof(mixed_out->pcm));

    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        if (!has_source[priority]) {
            continue;
        }

        const bool is_safety = priority == AUDIO_OUTPUT_PRIORITY_SAFETY;
        const uint8_t percent = is_safety ? safety_volume : normal_volume;
        if (!is_safety && percent == 0) {
            continue;
        }

        const audio_codec_pcm_frame_t *source_frame = &source[priority];
        for (size_t sample_index = 0;
             sample_index < AUDIO_CODEC_SAMPLES_PER_FRAME;
             ++sample_index) {
            mixed_out->pcm[sample_index] = audio_output_saturate_add(
                mixed_out->pcm[sample_index],
                audio_output_scale_sample(
                    source_frame->pcm[sample_index],
                    percent
                )
            );
        }
    }

    *applied_volume_percent_out =
        has_source[AUDIO_OUTPUT_PRIORITY_SAFETY]
            ? safety_volume
            : normal_volume;
}

static bool audio_output_mix_next_frame(void) {
    bool has_source[AUDIO_OUTPUT_PRIORITY_COUNT] = {false};
    audio_codec_pcm_frame_t source[AUDIO_OUTPUT_PRIORITY_COUNT];
    uint64_t completed_sequences[AUDIO_OUTPUT_PRIORITY_COUNT] = {0};
    size_t source_count = 0;

    if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        if (audio_output_queue_pop_locked(priority, &source[priority])) {
            has_source[priority] = true;
            completed_sequences[priority] =
                audio_output_lanes[priority].sequence -
                (audio_output_lanes[priority].count);
            source_count++;
        }
    }
    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        if (has_source[priority]) {
            audio_output_lanes[priority].completed_sequence =
                completed_sequences[priority];
        }
    }
    xSemaphoreGive(audio_output_mutex);

    if (source_count == 0) {
        return false;
    }

    audio_codec_pcm_frame_t mixed;
    memset(&mixed, 0, sizeof(mixed));
    uint8_t applied_volume_percent = 0;
    audio_output_render_frame(
        has_source,
        source,
        &mixed,
        &applied_volume_percent
    );

    if (!audio_pipeline_is_playing()) {
        const esp_err_t enable_result = audio_pipeline_set_playing(true);
        if (enable_result != ESP_OK) {
            if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) == pdTRUE) {
                audio_output_snapshot.output_errors++;
                xSemaphoreGive(audio_output_mutex);
            }
            return true;
        }
    }

    const audio_codec_error_t result = audio_pipeline_play_frame(&mixed);
    if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) == pdTRUE) {
        if (result == AUDIO_CODEC_OK) {
            audio_output_snapshot.output_frames++;
        } else {
            audio_output_snapshot.output_errors++;
        }
        audio_output_snapshot.mixed_frames++;
        audio_output_snapshot.applied_volume_percent = applied_volume_percent;
        xSemaphoreGive(audio_output_mutex);
    }
    return true;
}

static void audio_output_task(void *argument) {
    (void)argument;

    while (audio_output_running) {
        if (xSemaphoreTake(audio_output_signal, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        while (audio_output_running) {
            if (!audio_output_mix_next_frame()) {
                break;
            }
        }
    }

    if (audio_output_task_exit != NULL) {
        xSemaphoreGive(audio_output_task_exit);
    }
    audio_output_task_handle = NULL;
    vTaskDelete(NULL);
}

static void audio_output_free_lanes(void) {
    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        if (audio_output_lanes[priority].frames != NULL) {
            free(audio_output_lanes[priority].frames);
            audio_output_lanes[priority].frames = NULL;
        }
        audio_output_lanes[priority].head = 0;
        audio_output_lanes[priority].tail = 0;
        audio_output_lanes[priority].count = 0;
        audio_output_lanes[priority].sequence = 0;
        audio_output_lanes[priority].completed_sequence = 0;
        audio_output_lanes[priority].discarded_through_sequence = 0;
    }
}

static esp_err_t audio_output_allocate_lanes(void) {
    const size_t allocation_size =
        AUDIO_OUTPUT_QUEUE_CAPACITY * sizeof(audio_codec_pcm_frame_t);

    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        audio_output_lanes[priority].frames = heap_caps_malloc_prefer(
            allocation_size,
            2,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            MALLOC_CAP_8BIT
        );
        if (audio_output_lanes[priority].frames == NULL) {
            audio_output_free_lanes();
            return ESP_ERR_NO_MEM;
        }
        audio_output_lanes[priority].head = 0;
        audio_output_lanes[priority].tail = 0;
        audio_output_lanes[priority].count = 0;
    }
    return ESP_OK;
}

esp_err_t audio_output_init(void) {
    if (audio_output_ready) {
        return ESP_OK;
    }
    if (!audio_pipeline_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_output_mutex == NULL) {
        audio_output_mutex = xSemaphoreCreateMutex();
    }
    if (audio_output_signal == NULL) {
        audio_output_signal = xSemaphoreCreateBinary();
    }
    if (audio_output_task_exit == NULL) {
        audio_output_task_exit = xSemaphoreCreateBinary();
    }
    if (audio_output_mutex == NULL || audio_output_signal == NULL ||
        audio_output_task_exit == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = audio_output_allocate_lanes();
    if (result != ESP_OK) {
        return result;
    }

    memset(&audio_output_snapshot, 0, sizeof(audio_output_snapshot));
    audio_output_snapshot.is_ready = true;
    audio_output_running = true;
    if (xTaskCreate(
            audio_output_task,
            "audio_output",
            CONFIG_AUDIO_OUTPUT_TASK_STACK_SIZE,
            NULL,
            CONFIG_AUDIO_OUTPUT_TASK_PRIORITY,
            &audio_output_task_handle
        ) != pdPASS) {
        audio_output_running = false;
        audio_output_free_lanes();
        return ESP_ERR_NO_MEM;
    }

    audio_output_ready = true;
    return ESP_OK;
}

bool audio_output_is_ready(void) {
    return audio_output_ready;
}

void audio_output_shutdown(void) {
    bool has_resources = audio_output_ready ||
        audio_output_task_handle != NULL ||
        audio_output_mutex != NULL ||
        audio_output_signal != NULL ||
        audio_output_task_exit != NULL;
    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         !has_resources && priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        has_resources = audio_output_lanes[priority].frames != NULL;
    }
    if (!has_resources) {
        return;
    }

    audio_output_ready = false;
    audio_output_running = false;
    audio_output_signal_task();

    if (audio_output_task_handle != NULL) {
        if (xSemaphoreTake(
                audio_output_task_exit,
                pdMS_TO_TICKS(2000)
            ) != pdTRUE) {
            // The task can still be inside a bounded I2S write. Keep every
            // resource alive rather than deleting a mutex the task may use.
            ESP_LOGE(TAG, "mix task did not stop; keeping resources alive");
            return;
        }
        audio_output_task_handle = NULL;
    }

    audio_output_free_lanes();
    if (audio_output_mutex != NULL) {
        vSemaphoreDelete(audio_output_mutex);
        audio_output_mutex = NULL;
    }
    if (audio_output_signal != NULL) {
        vSemaphoreDelete(audio_output_signal);
        audio_output_signal = NULL;
    }
    if (audio_output_task_exit != NULL) {
        vSemaphoreDelete(audio_output_task_exit);
        audio_output_task_exit = NULL;
    }
    memset(&audio_output_snapshot, 0, sizeof(audio_output_snapshot));
}

audio_output_error_t audio_output_submit(
    const audio_codec_pcm_frame_t *frame,
    audio_output_priority_t priority
) {
    if (!audio_output_ready) {
        return AUDIO_OUTPUT_ERR_NOT_INITIALIZED;
    }
    if (frame == NULL || !audio_output_priority_is_valid(priority)) {
        return AUDIO_OUTPUT_ERR_INVALID_ARGUMENT;
    }
    if (frame->pcm_size != AUDIO_CODEC_FRAME_PCM_BYTES) {
        return AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE;
    }

    const TickType_t timeout =
        pdMS_TO_TICKS(CONFIG_AUDIO_OUTPUT_SUBMIT_TIMEOUT_MS);
    const TickType_t started = xTaskGetTickCount();

    for (;;) {
        if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
            return AUDIO_OUTPUT_ERR_INTERNAL;
        }
        const audio_output_error_t result =
            audio_output_queue_push_locked(priority, frame, NULL);
        if (result == AUDIO_OUTPUT_OK) {
            xSemaphoreGive(audio_output_mutex);
            audio_output_signal_task();
            return AUDIO_OUTPUT_OK;
        }
        audio_output_snapshot.queue_full_frames++;
        xSemaphoreGive(audio_output_mutex);

        if (timeout == 0 || xTaskGetTickCount() - started >= timeout) {
            return AUDIO_OUTPUT_ERR_QUEUE_FULL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

audio_output_error_t audio_output_submit_blocking(
    const audio_codec_pcm_frame_t *frame,
    audio_output_priority_t priority
) {
    if (!audio_output_ready) {
        return AUDIO_OUTPUT_ERR_NOT_INITIALIZED;
    }
    if (frame == NULL || !audio_output_priority_is_valid(priority)) {
        return AUDIO_OUTPUT_ERR_INVALID_ARGUMENT;
    }
    if (frame->pcm_size != AUDIO_CODEC_FRAME_PCM_BYTES) {
        return AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE;
    }

    const TickType_t timeout =
        pdMS_TO_TICKS(CONFIG_AUDIO_OUTPUT_SUBMIT_TIMEOUT_MS);
    const TickType_t started = xTaskGetTickCount();
    const TickType_t render_timeout =
        pdMS_TO_TICKS(CONFIG_AUDIO_OUTPUT_RENDER_TIMEOUT_MS);
    const TickType_t render_started = xTaskGetTickCount();
    uint64_t sequence = 0;

    for (;;) {
        if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
            return AUDIO_OUTPUT_ERR_INTERNAL;
        }
        const audio_output_error_t result =
            audio_output_queue_push_locked(priority, frame, &sequence);
        if (result == AUDIO_OUTPUT_OK) {
            xSemaphoreGive(audio_output_mutex);
            audio_output_signal_task();
            break;
        }
        audio_output_snapshot.queue_full_frames++;
        xSemaphoreGive(audio_output_mutex);

        if (timeout == 0 || xTaskGetTickCount() - started >= timeout) {
            return AUDIO_OUTPUT_ERR_QUEUE_FULL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    for (;;) {
        if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
            return AUDIO_OUTPUT_ERR_INTERNAL;
        }
        const bool completed =
            audio_output_lanes[priority].completed_sequence >= sequence;
        const bool discarded =
            audio_output_lanes[priority].discarded_through_sequence >=
            sequence;
        xSemaphoreGive(audio_output_mutex);
        if (completed) {
            return AUDIO_OUTPUT_OK;
        }
        if (discarded) {
            return AUDIO_OUTPUT_ERR_DISCARDED;
        }
        if (xTaskGetTickCount() - render_started >= render_timeout) {
            return AUDIO_OUTPUT_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

audio_output_error_t audio_output_discard_priority(
    audio_output_priority_t priority,
    size_t *discarded_frames_out
) {
    if (discarded_frames_out != NULL) {
        *discarded_frames_out = 0;
    }
    if (!audio_output_ready) {
        return AUDIO_OUTPUT_ERR_NOT_INITIALIZED;
    }
    if (!audio_output_priority_is_valid(priority)) {
        return AUDIO_OUTPUT_ERR_INVALID_ARGUMENT;
    }
    if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_OUTPUT_ERR_INTERNAL;
    }

    const size_t discarded = audio_output_queue_discard_locked(priority);
    audio_output_count_discarded_locked(discarded);
    if (discarded_frames_out != NULL) {
        *discarded_frames_out = discarded;
    }

    xSemaphoreGive(audio_output_mutex);
    return AUDIO_OUTPUT_OK;
}

audio_output_error_t audio_output_flush(
    audio_output_priority_t max_priority,
    bool include_safety,
    size_t *discarded_frames_out
) {
    if (discarded_frames_out != NULL) {
        *discarded_frames_out = 0;
    }
    if (!audio_output_ready) {
        return AUDIO_OUTPUT_ERR_NOT_INITIALIZED;
    }
    if (!audio_output_priority_is_valid(max_priority)) {
        return AUDIO_OUTPUT_ERR_INVALID_ARGUMENT;
    }
    if (xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_OUTPUT_ERR_INTERNAL;
    }

    size_t discarded = 0;
    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority <= max_priority;
         ++priority) {
        if (priority == AUDIO_OUTPUT_PRIORITY_SAFETY && !include_safety) {
            continue;
        }
        discarded += audio_output_queue_discard_locked(priority);
    }
    audio_output_count_discarded_locked(discarded);
    if (discarded_frames_out != NULL) {
        *discarded_frames_out = discarded;
    }

    xSemaphoreGive(audio_output_mutex);
    return AUDIO_OUTPUT_OK;
}

audio_output_snapshot_t audio_output_get_snapshot(void) {
    audio_output_snapshot_t snapshot = {0};
    if (!audio_output_ready ||
        xSemaphoreTake(audio_output_mutex, portMAX_DELAY) != pdTRUE) {
        return snapshot;
    }

    snapshot = audio_output_snapshot;
    for (audio_output_priority_t priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        snapshot.pending_frames[priority] =
            (uint32_t)audio_output_lanes[priority].count;
    }
#if AUDIO_OUTPUT_HAS_VOLUME_CONTROL
    snapshot.is_muted = volume_control_is_ready() &&
        volume_control_is_muted();
#endif
    xSemaphoreGive(audio_output_mutex);
    return snapshot;
}

const char *audio_output_priority_name(audio_output_priority_t priority) {
    switch (priority) {
        case AUDIO_OUTPUT_PRIORITY_SAFETY:
            return "safety";
        case AUDIO_OUTPUT_PRIORITY_PROMPT:
            return "prompt";
        case AUDIO_OUTPUT_PRIORITY_CONVERSATION:
            return "conversation";
        case AUDIO_OUTPUT_PRIORITY_AMBIENT:
        default:
            return "ambient";
    }
}

const char *audio_output_error_name(audio_output_error_t error) {
    switch (error) {
        case AUDIO_OUTPUT_OK:
            return "ok";
        case AUDIO_OUTPUT_ERR_NOT_INITIALIZED:
            return "audio_codec_not_initialized";
        case AUDIO_OUTPUT_ERR_INVALID_ARGUMENT:
            return "audio_codec_invalid_argument";
        case AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE:
            return "audio_codec_invalid_pcm_size";
        case AUDIO_OUTPUT_ERR_QUEUE_FULL:
            return "audio_queue_full";
        case AUDIO_OUTPUT_ERR_DISCARDED:
            return "audio_output_discarded";
        case AUDIO_OUTPUT_ERR_TIMEOUT:
            return "audio_output_timeout";
        case AUDIO_OUTPUT_ERR_INTERNAL:
        default:
            return "audio_codec_internal";
    }
}

const module_descriptor_t *audio_output_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "audio_output",
        .version = "1.0.0",
        .initialize = audio_output_init,
        .shutdown = audio_output_shutdown,
    };
    return &descriptor;
}
