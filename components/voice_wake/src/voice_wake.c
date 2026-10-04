#include "voice_wake.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "audio_pipeline.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER && __has_include("diagnostic_reporter.h")
#include "diagnostic_reporter.h"
#define VOICE_WAKE_HAS_DIAGNOSTIC_REPORTER 1
#else
#define VOICE_WAKE_HAS_DIAGNOSTIC_REPORTER 0
#endif

#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"
#endif

static const char *const TAG = "voice_wake";

#define VOICE_WAKE_MODEL_PARTITION "model"

typedef enum {
    VOICE_WAKE_REJECTION_INVALID_WORD = 0,
    VOICE_WAKE_REJECTION_LOW_CONFIDENCE,
    VOICE_WAKE_REJECTION_SUSPENDED,
    VOICE_WAKE_REJECTION_PLAYBACK,
    VOICE_WAKE_REJECTION_COOLDOWN,
    VOICE_WAKE_REJECTION_DISARMED,
    VOICE_WAKE_REJECTION_SUPPRESSED,
    VOICE_WAKE_REJECTION_COUNT,
} voice_wake_rejection_reason_t;

typedef struct {
    atomic_bool is_ready;
    atomic_bool is_armed;
    atomic_bool is_suspended;
    atomic_uint frames_processed;
    atomic_uint detections;
    atomic_uint rejected_frames;
    atomic_uint false_wake_rejections;
    atomic_uint last_confidence_milli;
    atomic_ullong last_detected_at_ms;
    atomic_uint session_nonce;
    atomic_ullong playback_stopped_at_ms;
    atomic_ullong cooldown_until_ms;
    atomic_ullong rejection_event_allowed_at_ms[VOICE_WAKE_REJECTION_COUNT];
    atomic_uint deterministic_run_frames;
    atomic_bool task_stop_requested;
    atomic_bool is_running;
    voice_wake_handler_t handler;
    void *handler_context;
    SemaphoreHandle_t lifecycle_mutex;
    SemaphoreHandle_t task_exit_semaphore;
    TaskHandle_t task_handle;
    int16_t *feed_buffer;
    size_t feed_sample_capacity;
    size_t feed_samples_pending;
#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
    srmodel_list_t *models;
    afe_config_t *afe_config;
    const esp_afe_sr_iface_t *afe_interface;
    esp_afe_sr_data_t *afe;
    char *wake_word_name;
    bool wake_word_name_owned;
#endif
} voice_wake_context_t;

static voice_wake_context_t voice_wake_state;
static portMUX_TYPE voice_wake_handler_lock =
    portMUX_INITIALIZER_UNLOCKED;

static uint64_t voice_wake_now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static uint32_t voice_wake_confidence_milli(void) {
#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
    return (uint32_t)CONFIG_VOICE_WAKE_WAKENET_THRESHOLD_MILLI;
#else
    return 1000u;
#endif
}

#if VOICE_WAKE_HAS_DIAGNOSTIC_REPORTER
static const char *voice_wake_rejection_reason_code(
    voice_wake_rejection_reason_t reason
) {
    switch (reason) {
        case VOICE_WAKE_REJECTION_INVALID_WORD:
            return "invalid_word";
        case VOICE_WAKE_REJECTION_LOW_CONFIDENCE:
            return "low_confidence";
        case VOICE_WAKE_REJECTION_SUSPENDED:
            return "suspended";
        case VOICE_WAKE_REJECTION_PLAYBACK:
            return "playback";
        case VOICE_WAKE_REJECTION_COOLDOWN:
            return "cooldown";
        case VOICE_WAKE_REJECTION_DISARMED:
            return "disarmed";
        case VOICE_WAKE_REJECTION_SUPPRESSED:
            return "suppressed";
        default:
            return "unknown";
    }
}
#endif

static void voice_wake_format_detail_code(
    char *output,
    size_t output_size,
    uint32_t wake_word_id,
    uint32_t confidence_milli
) {
    if (output == NULL || output_size == 0) {
        return;
    }
    output[0] = '\0';
    // The detail code is the outward diagnostic identity. Keep it ASCII-only
    // and bounded even when the ESP-SR model name contains Chinese text or
    // semicolons that the platform symbolic-text contract rejects.
    snprintf(
        output,
        output_size,
        "wake_%lu_confidence_%04lu",
        (unsigned long)wake_word_id,
        (unsigned long)(confidence_milli > 1000u ? 1000u : confidence_milli)
    );
}

// Rejection diagnostics are sampled rather than emitted per frame: the
// detector may reject many frames per second during playback or cooldown, and
// the interaction queue is deliberately small.
static void voice_wake_report_rejection(
    voice_wake_rejection_reason_t reason,
    uint32_t wake_word_id,
    uint32_t confidence_milli
) {
    atomic_fetch_add(&voice_wake_state.false_wake_rejections, 1u);
#if VOICE_WAKE_HAS_DIAGNOSTIC_REPORTER
    if (reason < 0 || reason >= VOICE_WAKE_REJECTION_COUNT) {
        return;
    }
    const uint64_t now_ms = voice_wake_now_ms();
    const uint64_t previous_allowed =
        atomic_load(&voice_wake_state.rejection_event_allowed_at_ms[reason]);
    if (now_ms < previous_allowed) {
        return;
    }
    atomic_store(
        &voice_wake_state.rejection_event_allowed_at_ms[reason],
        now_ms + CONFIG_VOICE_WAKE_REJECTION_EVENT_INTERVAL_MS
    );

    char detail_code[VOICE_WAKE_DETAIL_CODE_SIZE] = {0};
    if (reason == VOICE_WAKE_REJECTION_INVALID_WORD ||
        reason == VOICE_WAKE_REJECTION_LOW_CONFIDENCE) {
        voice_wake_format_detail_code(
            detail_code,
            sizeof(detail_code),
            wake_word_id,
            confidence_milli
        );
    } else {
        snprintf(
            detail_code,
            sizeof(detail_code),
            "wake_rejected_%s",
            voice_wake_rejection_reason_code(reason)
        );
    }
    (void)diagnostic_reporter_record_interaction(
        "wake_rejected",
        detail_code,
        0
    );
#endif
}

static bool voice_wake_word_is_valid(uint32_t wake_word_id) {
    return wake_word_id > 0;
}

static void voice_wake_publish_detection(
    uint32_t wake_word_id,
    const char *wake_word_name,
    uint32_t confidence_milli,
    uint64_t detected_at_ms
) {
    if (!voice_wake_word_is_valid(wake_word_id)) {
        voice_wake_report_rejection(
            VOICE_WAKE_REJECTION_INVALID_WORD,
            wake_word_id,
            confidence_milli
        );
        return;
    }
    if (confidence_milli < CONFIG_VOICE_WAKE_MIN_CONFIDENCE_MILLI) {
        voice_wake_report_rejection(
            VOICE_WAKE_REJECTION_LOW_CONFIDENCE,
            wake_word_id,
            confidence_milli
        );
        return;
    }
    if (atomic_load(&voice_wake_state.is_suspended)) {
        voice_wake_report_rejection(
            VOICE_WAKE_REJECTION_SUSPENDED,
            wake_word_id,
            confidence_milli
        );
        return;
    }
    if (audio_pipeline_is_playing() ||
        detected_at_ms < atomic_load(&voice_wake_state.playback_stopped_at_ms) +
            CONFIG_VOICE_WAKE_PLAYBACK_IGNORE_MS) {
        voice_wake_report_rejection(
            VOICE_WAKE_REJECTION_PLAYBACK,
            wake_word_id,
            confidence_milli
        );
        return;
    }
    if (detected_at_ms < atomic_load(&voice_wake_state.cooldown_until_ms)) {
        voice_wake_report_rejection(
            VOICE_WAKE_REJECTION_COOLDOWN,
            wake_word_id,
            confidence_milli
        );
        return;
    }
    if (!atomic_load(&voice_wake_state.is_armed)) {
        voice_wake_report_rejection(
            VOICE_WAKE_REJECTION_DISARMED,
            wake_word_id,
            confidence_milli
        );
        return;
    }

    voice_wake_event_t event = {0};
    event.wake_word_id = wake_word_id;
    event.confidence_milli = confidence_milli;
    event.detected_at_ms = detected_at_ms;
    event.session_nonce = atomic_load(&voice_wake_state.session_nonce);
    voice_wake_format_detail_code(
        event.detail_code,
        sizeof(event.detail_code),
        wake_word_id,
        confidence_milli
    );
    if (wake_word_name != NULL) {
        strncpy(
            event.wake_word_name,
            wake_word_name,
            sizeof(event.wake_word_name) - 1
        );
        event.wake_word_name[sizeof(event.wake_word_name) - 1] = '\0';
    }

    voice_wake_handler_t handler;
    void *handler_context;
    portENTER_CRITICAL(&voice_wake_handler_lock);
    handler = voice_wake_state.handler;
    handler_context = voice_wake_state.handler_context;
    portEXIT_CRITICAL(&voice_wake_handler_lock);
    if (handler != NULL) {
        handler(&event, handler_context);
    }

    atomic_store(
        &voice_wake_state.cooldown_until_ms,
        detected_at_ms + CONFIG_VOICE_WAKE_COOLDOWN_MS
    );
    atomic_store(&voice_wake_state.last_confidence_milli, confidence_milli);
    atomic_store(&voice_wake_state.last_detected_at_ms, detected_at_ms);
    atomic_fetch_add(&voice_wake_state.detections, 1u);
}

static bool voice_wake_is_suppressed(uint64_t now_ms) {
    if (!atomic_load(&voice_wake_state.is_armed) ||
        atomic_load(&voice_wake_state.is_suspended)) {
        return true;
    }
    if (audio_pipeline_is_playing()) {
        atomic_store(&voice_wake_state.playback_stopped_at_ms, now_ms);
        return true;
    }
    if (now_ms < atomic_load(&voice_wake_state.playback_stopped_at_ms) +
            CONFIG_VOICE_WAKE_PLAYBACK_IGNORE_MS) {
        return true;
    }
    return now_ms < atomic_load(&voice_wake_state.cooldown_until_ms);
}

static void voice_wake_increment_rejected_frame(void) {
    atomic_fetch_add(&voice_wake_state.rejected_frames, 1u);
}

#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR

static esp_err_t voice_wake_esp_sr_init(void) {
    voice_wake_state.models =
        esp_srmodel_init(VOICE_WAKE_MODEL_PARTITION);
    if (voice_wake_state.models == NULL) {
        ESP_LOGE(
            TAG,
            "model partition '%s' is missing or unreadable",
            VOICE_WAKE_MODEL_PARTITION
        );
        return ESP_ERR_NOT_FOUND;
    }

    char *model_name = esp_srmodel_filter(
        voice_wake_state.models,
        "wn",
        "nihaoxiaozhi"
    );
    if (model_name == NULL) {
        ESP_LOGE(TAG, "configured WakeNet model was not found");
        return ESP_ERR_NOT_FOUND;
    }

    voice_wake_state.afe_config = afe_config_init(
        "M",
        voice_wake_state.models,
        AFE_TYPE_SR,
#if CONFIG_VOICE_WAKE_AFE_MODE_HIGH_PERF
        AFE_MODE_HIGH_PERF
#else
        AFE_MODE_LOW_COST
#endif
    );
    if (voice_wake_state.afe_config == NULL) {
        ESP_LOGE(TAG, "AFE configuration allocation failed");
        return ESP_ERR_NO_MEM;
    }

    voice_wake_state.afe_config = afe_config_check(
        voice_wake_state.afe_config
    );
    if (voice_wake_state.afe_config == NULL) {
        ESP_LOGE(TAG, "AFE configuration validation failed");
        return ESP_ERR_INVALID_STATE;
    }
    voice_wake_state.afe_config->wakenet_model_name = model_name;
    voice_wake_state.afe_interface =
        esp_afe_handle_from_config(voice_wake_state.afe_config);
    if (voice_wake_state.afe_interface == NULL ||
        voice_wake_state.afe_interface->create_from_config == NULL) {
        ESP_LOGE(TAG, "AFE interface is unavailable");
        return ESP_ERR_NOT_SUPPORTED;
    }

    voice_wake_state.afe =
        voice_wake_state.afe_interface->create_from_config(
            voice_wake_state.afe_config
        );
    if (voice_wake_state.afe == NULL) {
        ESP_LOGE(TAG, "AFE creation failed");
        return ESP_ERR_NO_MEM;
    }
    if (voice_wake_state.afe_interface->get_feed_chunksize == NULL ||
        voice_wake_state.afe_interface->feed == NULL ||
        voice_wake_state.afe_interface->fetch_with_delay == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const int feed_samples =
        voice_wake_state.afe_interface->get_feed_chunksize(
            voice_wake_state.afe
        );
    if (feed_samples <= 0) {
        ESP_LOGE(TAG, "AFE feed size is invalid: %d", feed_samples);
        return ESP_ERR_INVALID_SIZE;
    }
    voice_wake_state.feed_buffer =
        calloc((size_t)feed_samples, sizeof(int16_t));
    if (voice_wake_state.feed_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    voice_wake_state.feed_sample_capacity = (size_t)feed_samples;
    voice_wake_state.feed_samples_pending = 0;

    voice_wake_state.wake_word_name =
        esp_srmodel_get_wake_words(voice_wake_state.models, model_name);
    if (voice_wake_state.wake_word_name != NULL) {
        voice_wake_state.wake_word_name_owned = true;
    } else {
        voice_wake_state.wake_word_name = model_name;
        voice_wake_state.wake_word_name_owned = false;
    }

    if (voice_wake_state.afe_interface->set_wakenet_threshold != NULL) {
        const float threshold =
            (float)CONFIG_VOICE_WAKE_WAKENET_THRESHOLD_MILLI / 1000.0f;
        (void)voice_wake_state.afe_interface->set_wakenet_threshold(
            voice_wake_state.afe,
            1,
            threshold
        );
    }
    return ESP_OK;
}

static void voice_wake_esp_sr_deinit(void) {
    if (voice_wake_state.afe != NULL &&
        voice_wake_state.afe_interface != NULL &&
        voice_wake_state.afe_interface->destroy != NULL) {
        voice_wake_state.afe_interface->destroy(voice_wake_state.afe);
    }
    voice_wake_state.afe = NULL;
    voice_wake_state.afe_interface = NULL;

    if (voice_wake_state.afe_config != NULL) {
        afe_config_free(voice_wake_state.afe_config);
        voice_wake_state.afe_config = NULL;
    }
    if (voice_wake_state.models != NULL) {
        esp_srmodel_deinit(voice_wake_state.models);
        voice_wake_state.models = NULL;
    }
    free(voice_wake_state.feed_buffer);
    voice_wake_state.feed_buffer = NULL;
    voice_wake_state.feed_sample_capacity = 0;
    voice_wake_state.feed_samples_pending = 0;
    if (voice_wake_state.wake_word_name_owned) {
        free(voice_wake_state.wake_word_name);
    }
    voice_wake_state.wake_word_name = NULL;
    voice_wake_state.wake_word_name_owned = false;
}

static void voice_wake_esp_sr_feed(
    const int16_t *samples,
    size_t sample_count,
    bool detection_allowed
) {
    if (voice_wake_state.feed_buffer == NULL ||
        voice_wake_state.feed_sample_capacity == 0) {
        return;
    }

    size_t sample_offset = 0;
    while (sample_offset < sample_count) {
        const size_t space_available =
            voice_wake_state.feed_sample_capacity -
            voice_wake_state.feed_samples_pending;
        const size_t remaining = sample_count - sample_offset;
        const size_t copy_count =
            remaining < space_available ? remaining : space_available;

        memcpy(
            voice_wake_state.feed_buffer +
                voice_wake_state.feed_samples_pending,
            samples + sample_offset,
            copy_count * sizeof(int16_t)
        );
        voice_wake_state.feed_samples_pending += copy_count;
        sample_offset += copy_count;

        if (voice_wake_state.feed_samples_pending <
            voice_wake_state.feed_sample_capacity) {
            continue;
        }

        voice_wake_state.feed_samples_pending = 0;
        if (voice_wake_state.afe_interface->feed(
                voice_wake_state.afe,
                voice_wake_state.feed_buffer
            ) <= 0) {
            voice_wake_increment_rejected_frame();
            continue;
        }

        afe_fetch_result_t *result =
            voice_wake_state.afe_interface->fetch_with_delay(
                voice_wake_state.afe,
                pdMS_TO_TICKS(CONFIG_VOICE_WAKE_FRAME_TIMEOUT_MS)
            );
        if (result == NULL || result->ret_value < 0) {
            voice_wake_increment_rejected_frame();
            continue;
        }
        if (result->wakeup_state != WAKENET_DETECTED ||
            detection_allowed == false) {
            if (result->wakeup_state == WAKENET_DETECTED) {
                voice_wake_report_rejection(
                    VOICE_WAKE_REJECTION_SUPPRESSED,
                    result->wake_word_index > 0
                        ? (uint32_t)result->wake_word_index
                        : (uint32_t)result->wakenet_model_index,
                    voice_wake_confidence_milli()
                );
            }
            continue;
        }

        voice_wake_publish_detection(
            result->wake_word_index > 0
                ? (uint32_t)result->wake_word_index
                : (uint32_t)result->wakenet_model_index,
            voice_wake_state.wake_word_name,
            voice_wake_confidence_milli(),
            voice_wake_now_ms()
        );
    }
}

#else

static void voice_wake_deterministic_reset(void) {
    atomic_store(&voice_wake_state.deterministic_run_frames, 0);
}

static uint32_t voice_wake_deterministic_rms(
    const int16_t *samples,
    size_t sample_count
) {
    uint64_t sum_squares = 0;
    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t sample = samples[index];
        sum_squares += (uint64_t)(sample * sample);
    }
    if (sample_count == 0) {
        return 0;
    }
    return (uint32_t)(sum_squares / sample_count);
}

static uint32_t voice_wake_deterministic_zero_crossings(
    const int16_t *samples,
    size_t sample_count
) {
    uint32_t crossings = 0;
    for (size_t index = 1; index < sample_count; ++index) {
        if ((samples[index - 1] < 0 && samples[index] >= 0) ||
            (samples[index - 1] >= 0 && samples[index] < 0)) {
            crossings++;
        }
    }
    return crossings;
}

static void voice_wake_deterministic_feed(
    const int16_t *samples,
    size_t sample_count
) {
    const uint32_t mean_square =
        voice_wake_deterministic_rms(samples, sample_count);
    const uint32_t rms = (uint32_t)sqrtf((float)mean_square);
    const uint32_t crossings =
        voice_wake_deterministic_zero_crossings(samples, sample_count);
    const bool speech_like =
        rms >= CONFIG_VOICE_WAKE_DETERMINISTIC_RMS_THRESHOLD &&
        crossings >= 4 &&
        crossings <= sample_count / 2;

    if (!speech_like) {
        voice_wake_deterministic_reset();
        voice_wake_increment_rejected_frame();
        return;
    }

    const uint32_t run_frames =
        atomic_fetch_add(&voice_wake_state.deterministic_run_frames, 1u) + 1u;
    if (run_frames < CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES) {
        return;
    }
    atomic_store(&voice_wake_state.deterministic_run_frames, 0);
    voice_wake_publish_detection(
        1,
        "deterministic_energy",
        1000,
        voice_wake_now_ms()
    );
}

#endif

static void voice_wake_task(void *argument) {
    (void)argument;
    audio_codec_pcm_frame_t frame;

    while (atomic_load(&voice_wake_state.task_stop_requested) == false) {
        if (atomic_load(&voice_wake_state.is_running) == false ||
            !audio_pipeline_is_capturing()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (audio_pipeline_capture_frame(
                AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE,
                &frame
            ) != AUDIO_CODEC_OK) {
            voice_wake_increment_rejected_frame();
            continue;
        }
        atomic_fetch_add(&voice_wake_state.frames_processed, 1u);

        const uint64_t now_ms = voice_wake_now_ms();
        const bool detection_allowed =
            voice_wake_is_suppressed(now_ms) == false;

#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
        voice_wake_esp_sr_feed(
            frame.pcm,
            frame.pcm_size / sizeof(int16_t),
            detection_allowed
        );
#else
        if (detection_allowed) {
            voice_wake_deterministic_feed(
                frame.pcm,
                frame.pcm_size / sizeof(int16_t)
            );
        }
#endif
    }

    if (voice_wake_state.task_exit_semaphore != NULL) {
        xSemaphoreGive(voice_wake_state.task_exit_semaphore);
    }
    vTaskDelete(NULL);
}

esp_err_t voice_wake_init(void) {
    if (voice_wake_state.lifecycle_mutex == NULL) {
        voice_wake_state.lifecycle_mutex = xSemaphoreCreateMutex();
        if (voice_wake_state.lifecycle_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (atomic_load(&voice_wake_state.is_ready)) {
        xSemaphoreGive(voice_wake_state.lifecycle_mutex);
        return ESP_OK;
    }
    if (!audio_pipeline_is_ready()) {
        xSemaphoreGive(voice_wake_state.lifecycle_mutex);
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
    result = voice_wake_esp_sr_init();
#else
    voice_wake_deterministic_reset();
#endif
    if (result == ESP_OK) {
        voice_wake_state.task_exit_semaphore =
            xSemaphoreCreateBinary();
        if (voice_wake_state.task_exit_semaphore == NULL) {
            result = ESP_ERR_NO_MEM;
        }
    }
    if (result == ESP_OK) {
        portENTER_CRITICAL(&voice_wake_handler_lock);
        voice_wake_state.handler = NULL;
        voice_wake_state.handler_context = NULL;
        portEXIT_CRITICAL(&voice_wake_handler_lock);
        atomic_store(&voice_wake_state.session_nonce, 0);
        atomic_store(&voice_wake_state.playback_stopped_at_ms, 0);
        atomic_store(&voice_wake_state.cooldown_until_ms, 0);
        for (size_t reason = 0;
             reason < VOICE_WAKE_REJECTION_COUNT;
             ++reason) {
            atomic_store(
                &voice_wake_state.rejection_event_allowed_at_ms[reason],
                0
            );
        }
        atomic_store(&voice_wake_state.deterministic_run_frames, 0);
        atomic_store(&voice_wake_state.is_armed, false);
        atomic_store(&voice_wake_state.is_suspended, false);
        atomic_store(&voice_wake_state.is_ready, true);
    } else {
#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
        voice_wake_esp_sr_deinit();
#endif
    }

    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
    return result;
}

bool voice_wake_is_ready(void) {
    return atomic_load(&voice_wake_state.is_ready);
}

static esp_err_t voice_wake_start_task_locked(void) {
    atomic_store(&voice_wake_state.task_stop_requested, false);
    atomic_store(&voice_wake_state.is_running, true);
    if (voice_wake_state.task_handle != NULL) {
        return ESP_OK;
    }
    (void)xSemaphoreTake(voice_wake_state.task_exit_semaphore, 0);
    if (xTaskCreate(
            voice_wake_task,
            "voice_wake",
            CONFIG_VOICE_WAKE_TASK_STACK_SIZE,
            NULL,
            CONFIG_VOICE_WAKE_TASK_PRIORITY,
            &voice_wake_state.task_handle
        ) != pdPASS) {
        atomic_store(&voice_wake_state.is_running, false);
        voice_wake_state.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t voice_wake_stop_task_locked(void) {
    atomic_store(&voice_wake_state.is_running, false);
    atomic_store(&voice_wake_state.task_stop_requested, true);
    const esp_err_t capture_result = audio_pipeline_capture_release(
        AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
    );
    if (voice_wake_state.task_handle == NULL) {
        return capture_result;
    }

    esp_err_t result = ESP_OK;
    if (xSemaphoreTake(
            voice_wake_state.task_exit_semaphore,
            pdMS_TO_TICKS(1000)
        ) != pdTRUE) {
        ESP_LOGE(TAG, "wake task did not stop in time");
        result = ESP_ERR_TIMEOUT;
    }
    if (result == ESP_OK) {
        voice_wake_state.task_handle = NULL;
    }
    if (capture_result != ESP_OK) {
        return capture_result;
    }
    return result;
}

esp_err_t voice_wake_start(void) {
    if (!atomic_load(&voice_wake_state.is_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (!audio_pipeline_is_ready()) {
        result = ESP_ERR_INVALID_STATE;
    } else if (atomic_load(&voice_wake_state.is_armed)) {
        result = ESP_OK;
    } else {
#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
        if (voice_wake_state.afe_interface->reset_buffer != NULL) {
            (void)voice_wake_state.afe_interface->reset_buffer(
                voice_wake_state.afe
            );
        }
#else
        voice_wake_deterministic_reset();
#endif
        uint32_t session_nonce =
            atomic_fetch_add(&voice_wake_state.session_nonce, 1u) + 1u;
        if (session_nonce == 0) {
            session_nonce = 1;
            atomic_store(&voice_wake_state.session_nonce, session_nonce);
        }
        atomic_store(&voice_wake_state.playback_stopped_at_ms, 0);
        atomic_store(&voice_wake_state.cooldown_until_ms, 0);
        atomic_store(&voice_wake_state.is_suspended, false);
        atomic_store(&voice_wake_state.is_armed, true);
        result = voice_wake_start_task_locked();
        if (result == ESP_OK) {
            result = audio_pipeline_capture_acquire(
                AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
            );
        }
        if (result != ESP_OK) {
            atomic_store(&voice_wake_state.is_armed, false);
            (void)voice_wake_stop_task_locked();
        }
    }

    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
    return result;
}

esp_err_t voice_wake_stop(void) {
    if (!atomic_load(&voice_wake_state.is_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(&voice_wake_state.is_armed, false);
    atomic_store(&voice_wake_state.is_suspended, false);
    const esp_err_t result = voice_wake_stop_task_locked();
    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
    return result;
}

esp_err_t voice_wake_suspend(void) {
    if (!atomic_load(&voice_wake_state.is_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (!atomic_load(&voice_wake_state.is_armed)) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!atomic_load(&voice_wake_state.is_suspended)) {
        atomic_store(&voice_wake_state.is_suspended, true);
        result = audio_pipeline_capture_release(
            AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
        );
    }

    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
    return result;
}

esp_err_t voice_wake_resume(void) {
    if (!atomic_load(&voice_wake_state.is_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (!atomic_load(&voice_wake_state.is_armed)) {
        result = ESP_ERR_INVALID_STATE;
    } else if (atomic_load(&voice_wake_state.is_suspended)) {
        result = voice_wake_start_task_locked();
        if (result == ESP_OK) {
#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
            if (voice_wake_state.afe_interface->reset_buffer != NULL) {
                (void)voice_wake_state.afe_interface->reset_buffer(
                    voice_wake_state.afe
                );
            }
#else
            voice_wake_deterministic_reset();
#endif
            result = audio_pipeline_capture_acquire(
                AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
            );
        }
        if (result == ESP_OK) {
            atomic_store(&voice_wake_state.is_suspended, false);
        }
    }

    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
    return result;
}

esp_err_t voice_wake_set_wake_handler(
    voice_wake_handler_t handler,
    void *context
) {
    if (!atomic_load(&voice_wake_state.is_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&voice_wake_handler_lock);
    voice_wake_state.handler = handler;
    voice_wake_state.handler_context = context;
    portEXIT_CRITICAL(&voice_wake_handler_lock);
    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
    return ESP_OK;
}

esp_err_t voice_wake_get_snapshot(voice_wake_snapshot_t *snapshot_out) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->is_ready = atomic_load(&voice_wake_state.is_ready);
    snapshot_out->is_armed = atomic_load(&voice_wake_state.is_armed);
    snapshot_out->is_suspended =
        atomic_load(&voice_wake_state.is_suspended);
    snapshot_out->frames_processed =
        atomic_load(&voice_wake_state.frames_processed);
    snapshot_out->detections =
        atomic_load(&voice_wake_state.detections);
    snapshot_out->rejected_frames =
        atomic_load(&voice_wake_state.rejected_frames);
    snapshot_out->false_wake_rejections =
        atomic_load(&voice_wake_state.false_wake_rejections);
    snapshot_out->last_confidence_milli =
        atomic_load(&voice_wake_state.last_confidence_milli);
    snapshot_out->last_detected_at_ms =
        atomic_load(&voice_wake_state.last_detected_at_ms);
    return ESP_OK;
}

const char *voice_wake_error_name(voice_wake_error_t error) {
    switch (error) {
        case VOICE_WAKE_ERROR_NONE:
            return "none";
        case VOICE_WAKE_ERROR_NOT_INITIALIZED:
            return "voice_wake_not_initialized";
        case VOICE_WAKE_ERROR_INVALID_ARGUMENT:
            return "voice_wake_invalid_argument";
        case VOICE_WAKE_ERROR_INVALID_STATE:
            return "voice_wake_invalid_state";
        case VOICE_WAKE_ERROR_NO_MEMORY:
            return "voice_wake_no_memory";
        case VOICE_WAKE_ERROR_BACKEND:
        default:
            return "voice_wake_backend_error";
    }
}

static void voice_wake_shutdown(void) {
    if (voice_wake_state.lifecycle_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(voice_wake_state.lifecycle_mutex, portMAX_DELAY)
        != pdTRUE) {
        return;
    }

    atomic_store(&voice_wake_state.is_armed, false);
    atomic_store(&voice_wake_state.is_suspended, false);
    (void)voice_wake_stop_task_locked();
#if CONFIG_VOICE_WAKE_BACKEND_ESP_SR
    voice_wake_esp_sr_deinit();
#endif
    if (voice_wake_state.task_exit_semaphore != NULL) {
        vSemaphoreDelete(voice_wake_state.task_exit_semaphore);
        voice_wake_state.task_exit_semaphore = NULL;
    }
    atomic_store(&voice_wake_state.is_ready, false);
    xSemaphoreGive(voice_wake_state.lifecycle_mutex);
}

const module_descriptor_t *voice_wake_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "voice_wake",
        .version = "1.0.0",
        .initialize = voice_wake_init,
        .shutdown = voice_wake_shutdown,
    };
    return &descriptor;
}
