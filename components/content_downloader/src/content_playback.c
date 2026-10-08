#include "content_downloader.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "content_downloader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "playback_queue.h"

#include "content_playback_chunker.h"

#if CONFIG_FEATURE_PARENT_CONTROL_RUNTIME && __has_include("parent_control_runtime.h")
#include "parent_control_runtime.h"
#include "time_sync.h"
#include "usage_ledger.h"
#define CONTENT_PLAYBACK_HAS_PARENT_CONTROL_RUNTIME 1
#else
#define CONTENT_PLAYBACK_HAS_PARENT_CONTROL_RUNTIME 0
#endif

#ifndef CONFIG_CONTENT_PLAYBACK_CHUNK_FRAMES
#define CONFIG_CONTENT_PLAYBACK_CHUNK_FRAMES 32
#endif

#ifndef CONFIG_CONTENT_PLAYBACK_QUEUE_DEPTH
#define CONFIG_CONTENT_PLAYBACK_QUEUE_DEPTH 2
#endif

static const char *const TAG = "content_playback";

static bool content_playback_ready;
static content_playback_progress_t content_playback_progress;
static SemaphoreHandle_t content_playback_lock;
static TaskHandle_t content_playback_task_handle;
static volatile bool content_playback_stop_requested;
static volatile bool content_playback_active_play_requested;
static char content_playback_requested_package[CONTENT_LIBRARY_ID_SIZE];

static esp_err_t content_playback_submit_chunk(
    content_playback_chunk_t *chunk
) {
    if (chunk == NULL || chunk->frame_count == 0) {
        return ESP_OK;
    }
    playback_queue_item_t item = {};
    snprintf(item.item_id, sizeof(item.item_id), "%s", chunk->item_id);
    item.priority = PLAYBACK_PRIORITY_AMBIENT;
    item.is_interruptible = true;
    item.frame_count = chunk->frame_count;
    memcpy(item.frames, chunk->frames, sizeof(item.frames));
    playback_queue_error_t queue_result = PLAYBACK_QUEUE_ERR_FULL;
    for (int attempt = 0; attempt < 100; ++attempt) {
        queue_result = playback_queue_enqueue(&item);
        if (queue_result == PLAYBACK_QUEUE_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (queue_result == PLAYBACK_QUEUE_ERR_INTERRUPT_DENIED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (queue_result != PLAYBACK_QUEUE_OK) {
        return ESP_ERR_NO_MEM;
    }
    content_playback_chunker_clear_chunk(chunk);
    return ESP_OK;
}

static void content_playback_worker(void *argument) {
    (void)argument;
    while (!content_playback_stop_requested) {
        char package_id[CONTENT_LIBRARY_ID_SIZE] = {0};
        xSemaphoreTake(content_playback_lock, portMAX_DELAY);
        memcpy(
            package_id,
            content_playback_requested_package,
            sizeof(package_id)
        );
        content_playback_requested_package[0] = '\0';
        xSemaphoreGive(content_playback_lock);
        if (package_id[0] == '\0') {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        content_library_entry_t entry = {};
        if (content_library_get(package_id, &entry) != ESP_OK ||
            entry.state != CONTENT_LIBRARY_STATE_READY) {
            xSemaphoreTake(content_playback_lock, portMAX_DELAY);
            content_playback_progress.playing = false;
            xSemaphoreGive(content_playback_lock);
            continue;
        }
#if CONTENT_PLAYBACK_HAS_PARENT_CONTROL_RUNTIME
        parent_control_decision_t decision = {};
        const parent_control_request_t policy_request = {
            .category = entry.category,
            .safety_exempt = false,
            .utc_epoch_seconds = (int64_t)time(NULL),
            .timezone_offset_minutes =
                time_sync_get_timezone_offset_minutes(),
        };
        if (parent_control_evaluate(&policy_request, &decision) != ESP_OK ||
            parent_control_decision_is_denied(&decision)) {
            xSemaphoreTake(content_playback_lock, portMAX_DELAY);
            content_playback_progress.playing = false;
            xSemaphoreGive(content_playback_lock);
            continue;
        }
#endif
        char path[CONTENT_LIBRARY_PATH_SIZE] = {0};
        if (content_downloader_get_local_path(
                package_id,
                path,
                sizeof(path)
            ) != ESP_OK) {
            continue;
        }
        FILE *file = fopen(path, "rb");
        if (file == NULL) {
            ESP_LOGW(TAG, "content file is unavailable");
            continue;
        }
        xSemaphoreTake(content_playback_lock, portMAX_DELAY);
        content_playback_progress.playing = true;
        memcpy(
            content_playback_progress.package_id,
            package_id,
            sizeof(content_playback_progress.package_id)
        );
        content_playback_progress.queued_frames = 0;
        content_playback_progress.total_frames = 0;
        xSemaphoreGive(content_playback_lock);

        content_playback_chunker_t chunker = {};
        content_playback_chunker_init(
            &chunker,
            CONFIG_CONTENT_PLAYBACK_CHUNK_FRAMES,
            entry.size_bytes / sizeof(audio_codec_pcm_frame_t)
        );
        content_playback_chunk_t chunk = {};
        audio_codec_pcm_frame_t frame = {};
        bool canceled = false;
        while (fread(&frame, sizeof(frame), 1, file) == 1) {
            xSemaphoreTake(content_playback_lock, portMAX_DELAY);
            const bool should_stop =
                content_playback_requested_package[0] != '\0' ||
                !content_playback_active_play_requested;
            xSemaphoreGive(content_playback_lock);
            if (should_stop || content_playback_stop_requested) {
                canceled = true;
                break;
            }
#if CONTENT_PLAYBACK_HAS_PARENT_CONTROL_RUNTIME
            // Re-check the policy while the file plays so a guardian refresh
            // that starts a disabled period or reaches the daily limit stops
            // the current title promptly instead of finishing it.
            parent_control_decision_t mid_decision = {};
            if (parent_control_evaluate_active(
                    entry.category,
                    (int64_t)time(NULL),
                    time_sync_get_timezone_offset_minutes(),
                    &mid_decision
                ) == ESP_OK &&
                parent_control_decision_is_denied(&mid_decision)) {
                canceled = true;
                break;
            }
#endif
            if (content_playback_chunker_add(&chunker, &frame, &chunk)) {
                if (content_playback_submit_chunk(&chunk) != ESP_OK) {
                    break;
                }
                xSemaphoreTake(content_playback_lock, portMAX_DELAY);
                content_playback_progress.queued_frames =
                    chunker.queued_frames;
                xSemaphoreGive(content_playback_lock);
            }
        }
        if (!canceled &&
            !content_playback_chunker_is_empty(&chunker, &chunk)) {
            (void)content_playback_submit_chunk(&chunk);
        }
        fclose(file);
#if CONTENT_PLAYBACK_HAS_PARENT_CONTROL_RUNTIME
        if (!canceled) {
            const uint32_t frame_bytes = sizeof(audio_codec_pcm_frame_t);
            const uint32_t total_frames = frame_bytes == 0
                ? 0
                : (uint32_t)(entry.size_bytes / frame_bytes);
            const uint32_t content_seconds =
                (total_frames * AUDIO_CODEC_FRAME_DURATION_MS) / 1000u;
            (void)usage_ledger_record_content_playback(
                (int64_t)time(NULL),
                time_sync_get_timezone_offset_minutes(),
                entry.category,
                content_seconds
            );
        }
#endif
        xSemaphoreTake(content_playback_lock, portMAX_DELAY);
        content_playback_progress.playing = false;
        content_playback_progress.queued_frames = chunker.queued_frames;
        xSemaphoreGive(content_playback_lock);
    }
    content_playback_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t content_playback_init(void) {
    if (content_playback_ready) {
        return ESP_OK;
    }
    if (!playback_queue_is_ready() || !content_library_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    content_playback_lock = xSemaphoreCreateMutex();
    if (content_playback_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    content_playback_stop_requested = false;
    content_playback_active_play_requested = false;
    if (xTaskCreate(
            content_playback_worker,
            "sprout_content_play",
            6144,
            NULL,
            2,
            &content_playback_task_handle
        ) != pdPASS) {
        vSemaphoreDelete(content_playback_lock);
        content_playback_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    content_playback_ready = true;
    return ESP_OK;
}

bool content_playback_is_ready(void) {
    return content_playback_ready;
}

esp_err_t content_playback_start(const char *package_id) {
    if (!content_playback_ready || package_id == NULL ||
        package_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    content_library_entry_t entry = {};
    if (content_library_get(package_id, &entry) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (entry.state != CONTENT_LIBRARY_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(content_playback_lock, portMAX_DELAY);
    snprintf(
        content_playback_requested_package,
        sizeof(content_playback_requested_package),
        "%s",
        package_id
    );
    content_playback_active_play_requested = true;
    xSemaphoreGive(content_playback_lock);
    return ESP_OK;
}

esp_err_t content_playback_stop(void) {
    if (!content_playback_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(content_playback_lock, portMAX_DELAY);
    content_playback_requested_package[0] = '\0';
    content_playback_active_play_requested = false;
    content_playback_progress.playing = false;
    xSemaphoreGive(content_playback_lock);
    return ESP_OK;
}

esp_err_t content_playback_get_progress(
    content_playback_progress_t *progress_out
) {
    if (!content_playback_ready || progress_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(content_playback_lock, portMAX_DELAY);
    *progress_out = content_playback_progress;
    xSemaphoreGive(content_playback_lock);
    return ESP_OK;
}

const char *content_playback_error_name(content_playback_error_t error) {
    switch (error) {
        case CONTENT_PLAYBACK_OK:
            return "ok";
        case CONTENT_PLAYBACK_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case CONTENT_PLAYBACK_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case CONTENT_PLAYBACK_ERR_PACKAGE_NOT_READY:
            return "package_not_ready";
        case CONTENT_PLAYBACK_ERR_STORAGE:
            return "storage";
        case CONTENT_PLAYBACK_ERR_QUEUE_FULL:
            return "queue_full";
        case CONTENT_PLAYBACK_ERR_CANCELED:
            return "canceled";
        default:
            return "unknown";
    }
}

const module_descriptor_t *content_playback_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "content_playback",
        .version = "1.0.0",
        .initialize = content_playback_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
