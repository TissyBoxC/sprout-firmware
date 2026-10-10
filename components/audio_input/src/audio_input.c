#include "audio_input.h"

#include <string.h>

#include "audio_input_dsp.h"
#include "audio_pipeline.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *const TAG = "audio_input";

#define AUDIO_INPUT_PREROLL_FRAMES 2

typedef struct {
    int16_t frames[CONFIG_AUDIO_INPUT_REFERENCE_QUEUE_FRAMES]
                  [AUDIO_INPUT_FRAME_SAMPLES];
    size_t read_index;
    size_t count;
    SemaphoreHandle_t mutex;
    uint32_t dropped_frames;
} audio_input_reference_queue_t;

typedef struct {
    volatile bool is_running;
    bool initialized;
    uint32_t stream_id;
    uint32_t next_sequence;
    audio_input_frame_callback_t callback;
    audio_input_metrics_callback_t metrics_callback;
    void *callback_context;

    audio_input_dsp_t dsp;
    audio_input_reference_queue_t reference_queue;

    int16_t preroll[AUDIO_INPUT_PREROLL_FRAMES][AUDIO_INPUT_FRAME_SAMPLES];
    size_t preroll_write;
    size_t preroll_count;
    bool speech_active;

    audio_input_snapshot_t snapshot;
    SemaphoreHandle_t lifecycle_mutex;
    SemaphoreHandle_t task_exit_semaphore;
    TaskHandle_t task_handle;
} audio_input_context_t;

static audio_input_context_t audio_input_state;
static portMUX_TYPE audio_input_counter_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t audio_input_stop_locked(void);

static bool audio_input_reference_queue_push(
    audio_input_reference_queue_t *queue,
    const int16_t *samples
) {
    if (queue == NULL || samples == NULL) {
        return false;
    }
    // The push runs on the playback task, so it must never block. A contended
    // mutex drops the frame and is counted instead of stalling audio output.
    if (xSemaphoreTake(queue->mutex, 0) != pdTRUE) {
        __atomic_fetch_add(&queue->dropped_frames, 1u, __ATOMIC_RELAXED);
        return false;
    }
    if (queue->count == CONFIG_AUDIO_INPUT_REFERENCE_QUEUE_FRAMES) {
        queue->read_index =
            (queue->read_index + 1) % CONFIG_AUDIO_INPUT_REFERENCE_QUEUE_FRAMES;
        queue->count--;
        queue->dropped_frames++;
    }
    const size_t write_index =
        (queue->read_index + queue->count) %
        CONFIG_AUDIO_INPUT_REFERENCE_QUEUE_FRAMES;
    memcpy(
        queue->frames[write_index],
        samples,
        sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES
    );
    queue->count++;
    xSemaphoreGive(queue->mutex);
    return true;
}

static bool audio_input_reference_queue_pop(
    audio_input_reference_queue_t *queue,
    size_t delay_frames,
    int16_t *samples_out
) {
    if (queue == NULL || samples_out == NULL) {
        return false;
    }
    if (xSemaphoreTake(queue->mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    // Keep only the frame that is delay_frames behind the newest; discarding
    // older frames bounds latency if playback and capture drift apart.
    const size_t required = delay_frames + 1;
    while (queue->count > required) {
        queue->read_index =
            (queue->read_index + 1) % CONFIG_AUDIO_INPUT_REFERENCE_QUEUE_FRAMES;
        queue->count--;
        queue->dropped_frames++;
    }
    if (queue->count < required) {
        xSemaphoreGive(queue->mutex);
        return false;
    }
    memcpy(
        samples_out,
        queue->frames[queue->read_index],
        sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES
    );
    queue->read_index =
        (queue->read_index + 1) % CONFIG_AUDIO_INPUT_REFERENCE_QUEUE_FRAMES;
    queue->count--;
    xSemaphoreGive(queue->mutex);
    return true;
}

static void audio_input_reference_queue_flush(
    audio_input_reference_queue_t *queue
) {
    if (queue == NULL) {
        return;
    }
    if (xSemaphoreTake(queue->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    queue->read_index = 0;
    queue->count = 0;
    xSemaphoreGive(queue->mutex);
}

static void audio_input_reference_sink(
    const int16_t *samples,
    size_t sample_count,
    void *context
) {
    (void)context;
    if (samples == NULL || sample_count != AUDIO_INPUT_FRAME_SAMPLES) {
        return;
    }
    audio_input_reference_queue_push(&audio_input_state.reference_queue, samples);
}

static void audio_input_increment(uint32_t *counter) {
    portENTER_CRITICAL(&audio_input_counter_lock);
    (*counter)++;
    portEXIT_CRITICAL(&audio_input_counter_lock);
}

static void audio_input_publish_dsp_snapshot(void) {
    portENTER_CRITICAL(&audio_input_counter_lock);
    audio_input_state.snapshot.aec_convergence_q10 =
        audio_input_dsp_aec_convergence_q10(&audio_input_state.dsp);
    audio_input_state.snapshot.agc_gain_q8 =
        audio_input_dsp_agc_gain_q8(&audio_input_state.dsp);
    portEXIT_CRITICAL(&audio_input_counter_lock);
}

static void audio_input_emit_packet(
    const int16_t *pcm,
    uint64_t captured_at_ms,
    uint16_t microphone_level_q15,
    uint16_t reference_level_q15,
    bool has_reference,
    bool is_speech,
    bool report_metrics
) {
    audio_codec_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.stream_id = audio_input_state.stream_id;
    packet.sequence = audio_input_state.next_sequence++;
    packet.captured_at_ms = captured_at_ms;

    const audio_codec_error_t result =
        audio_codec_encode_frame(pcm, AUDIO_INPUT_FRAME_SAMPLES, &packet);
    if (result != AUDIO_CODEC_OK) {
        audio_input_increment(&audio_input_state.snapshot.dropped_frames);
        return;
    }
    audio_input_increment(&audio_input_state.snapshot.encoded_frames);

    // The callback owns the packet only for this call, so a consumer that
    // queues the audio must copy it first.
    if (audio_input_state.callback != NULL) {
        if (report_metrics && audio_input_state.metrics_callback != NULL) {
            audio_input_frame_metrics_t metrics = {
                .microphone_level_q15 = microphone_level_q15,
                .reference_level_q15 = reference_level_q15,
                .is_speech = is_speech,
                .has_reference = has_reference,
            };
            audio_input_state.metrics_callback(
                &metrics,
                audio_input_state.callback_context
            );
        }
        audio_input_state.callback(&packet, audio_input_state.callback_context);
    }
}

static uint16_t audio_input_level_q15(const int16_t *samples, size_t count) {
    if (samples == NULL || count == 0) {
        return 0;
    }
    uint64_t sum_squares = 0;
    for (size_t index = 0; index < count; ++index) {
        const int32_t sample = samples[index];
        sum_squares += (uint64_t)(sample * sample);
    }
    const uint32_t mean_square = (uint32_t)(
        sum_squares / (uint64_t)count
    );
    uint32_t root = 0;
    uint32_t bit = 1u << 15;
    while (bit > 0) {
        const uint32_t candidate = root | bit;
        if (candidate != 0 && candidate <= mean_square / candidate) {
            root = candidate;
        }
        bit >>= 1;
    }
    const uint32_t scaled = (root * 32767u) / 32768u;
    return scaled > 32767u ? 32767u : (uint16_t)scaled;
}

static uint16_t audio_input_microphone_level_q15(const audio_codec_pcm_frame_t *frame) {
    if (frame == NULL || frame->pcm_size < sizeof(int16_t)) {
        return 0;
    }
    const size_t sample_count = frame->pcm_size / sizeof(int16_t);
    return audio_input_level_q15(frame->pcm, sample_count);
}

static void audio_input_capture_task(void *argument) {
    (void)argument;
    audio_codec_pcm_frame_t captured;
    int16_t reference[AUDIO_INPUT_FRAME_SAMPLES];
    int16_t processed[AUDIO_INPUT_FRAME_SAMPLES];

    while (audio_input_state.is_running) {
        if (!audio_pipeline_is_capturing()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (audio_pipeline_capture_frame(
                AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT,
                &captured
            ) != AUDIO_CODEC_OK) {
            audio_input_increment(&audio_input_state.snapshot.dropped_frames);
            continue;
        }
        audio_input_increment(&audio_input_state.snapshot.captured_frames);

        const uint64_t captured_at_ms =
            (uint64_t)(esp_timer_get_time() / 1000);
        const bool has_reference = audio_input_reference_queue_pop(
            &audio_input_state.reference_queue,
            (size_t)CONFIG_AUDIO_INPUT_AEC_REFERENCE_DELAY_FRAMES,
            reference
        );
        const uint16_t microphone_level_q15 =
            audio_input_microphone_level_q15(&captured);
        const uint16_t reference_level_q15 = has_reference
            ? audio_input_level_q15(reference, AUDIO_INPUT_FRAME_SAMPLES)
            : 0;
        portENTER_CRITICAL(&audio_input_counter_lock);
        audio_input_state.snapshot.microphone_level_q15 =
            microphone_level_q15;
        audio_input_state.snapshot.reference_level_q15 =
            reference_level_q15;
        portEXIT_CRITICAL(&audio_input_counter_lock);

        bool is_speech = false;
        audio_input_dsp_process_frame(
            &audio_input_state.dsp,
            captured.pcm,
            has_reference ? reference : NULL,
            has_reference,
            processed,
            &is_speech
        );
        if (audio_input_state.dsp.aec_double_talk) {
            audio_input_increment(&audio_input_state.snapshot.double_talk_frames);
        }

        if (is_speech) {
            audio_input_increment(&audio_input_state.snapshot.speech_frames);
            if (!audio_input_state.speech_active) {
                // Flush the pre-roll so the first syllable is not clipped, in
                // chronological order with the timestamps it was captured at.
                for (size_t index = 0;
                     index < audio_input_state.preroll_count;
                     ++index) {
                    const size_t slot =
                        (audio_input_state.preroll_write +
                         AUDIO_INPUT_PREROLL_FRAMES -
                         audio_input_state.preroll_count + index) %
                        AUDIO_INPUT_PREROLL_FRAMES;
                    const uint64_t lag =
                        (uint64_t)(audio_input_state.preroll_count - index) *
                        AUDIO_CODEC_FRAME_DURATION_MS;
                    audio_input_emit_packet(
                        audio_input_state.preroll[slot],
                        captured_at_ms > lag ? captured_at_ms - lag : 0,
                        0,
                        0,
                        false,
                        true,
                        false
                    );
                }
                audio_input_state.preroll_count = 0;
                audio_input_state.speech_active = true;
            }
            audio_input_emit_packet(
                processed,
                captured_at_ms,
                microphone_level_q15,
                reference_level_q15,
                has_reference,
                true,
                true
            );
        } else {
            audio_input_increment(&audio_input_state.snapshot.suppressed_frames);
            audio_input_state.speech_active = false;
            if (audio_input_state.metrics_callback != NULL) {
                const audio_input_frame_metrics_t metrics = {
                    .microphone_level_q15 = microphone_level_q15,
                    .reference_level_q15 = reference_level_q15,
                    .is_speech = false,
                    .has_reference = has_reference,
                };
                audio_input_state.metrics_callback(
                    &metrics,
                    audio_input_state.callback_context
                );
            }
            memcpy(
                audio_input_state.preroll[audio_input_state.preroll_write],
                processed,
                sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES
            );
            audio_input_state.preroll_write =
                (audio_input_state.preroll_write + 1) % AUDIO_INPUT_PREROLL_FRAMES;
            if (audio_input_state.preroll_count < AUDIO_INPUT_PREROLL_FRAMES) {
                audio_input_state.preroll_count++;
            }
        }
        audio_input_publish_dsp_snapshot();
    }

    if (audio_input_state.task_exit_semaphore != NULL) {
        xSemaphoreGive(audio_input_state.task_exit_semaphore);
    }
    audio_input_state.task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_input_init(void) {
    if (audio_input_state.lifecycle_mutex == NULL) {
        audio_input_state.lifecycle_mutex = xSemaphoreCreateMutex();
        if (audio_input_state.lifecycle_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(audio_input_state.lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (audio_input_state.initialized) {
        xSemaphoreGive(audio_input_state.lifecycle_mutex);
        return ESP_OK;
    }
    if (!audio_codec_is_ready() || !audio_pipeline_is_ready()) {
        // The capture path cannot work without the codec and I2S pipeline, so
        // report an invalid state and let the registry retry after them.
        xSemaphoreGive(audio_input_state.lifecycle_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    result = audio_input_dsp_init(&audio_input_state.dsp);
    if (result == ESP_OK) {
        audio_input_state.reference_queue.mutex = xSemaphoreCreateMutex();
        audio_input_state.task_exit_semaphore = xSemaphoreCreateBinary();
        if (audio_input_state.reference_queue.mutex == NULL ||
            audio_input_state.task_exit_semaphore == NULL) {
            result = ESP_ERR_NO_MEM;
        }
    }
    if (result != ESP_OK) {
        audio_input_dsp_deinit(&audio_input_state.dsp);
        if (audio_input_state.reference_queue.mutex != NULL) {
            vSemaphoreDelete(audio_input_state.reference_queue.mutex);
            audio_input_state.reference_queue.mutex = NULL;
        }
        if (audio_input_state.task_exit_semaphore != NULL) {
            vSemaphoreDelete(audio_input_state.task_exit_semaphore);
            audio_input_state.task_exit_semaphore = NULL;
        }
        xSemaphoreGive(audio_input_state.lifecycle_mutex);
        return result;
    }

    memset(&audio_input_state.snapshot, 0, sizeof(audio_input_state.snapshot));
    audio_input_state.preroll_write = 0;
    audio_input_state.preroll_count = 0;
    audio_input_state.speech_active = false;
    audio_input_state.initialized = true;
    xSemaphoreGive(audio_input_state.lifecycle_mutex);
    return ESP_OK;
}

esp_err_t audio_input_start(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    void *context
) {
    return audio_input_start_with_metrics(
        stream_id,
        callback,
        NULL,
        context
    );
}

esp_err_t audio_input_start_with_metrics(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    audio_input_metrics_callback_t metrics_callback,
    void *context
) {
    if (audio_input_state.lifecycle_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(audio_input_state.lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (!audio_input_state.initialized) {
        result = ESP_ERR_INVALID_STATE;
    } else if (audio_input_state.is_running) {
        result = ESP_ERR_INVALID_STATE;
    } else if (callback == NULL) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        audio_input_reference_queue_flush(&audio_input_state.reference_queue);
        audio_input_dsp_reset_session(&audio_input_state.dsp);
        audio_input_state.preroll_write = 0;
        audio_input_state.preroll_count = 0;
        audio_input_state.speech_active = false;
        audio_input_state.stream_id = stream_id;
        audio_input_state.next_sequence = 0;
        audio_input_state.callback = callback;
        audio_input_state.metrics_callback = metrics_callback;
        audio_input_state.callback_context = context;

        portENTER_CRITICAL(&audio_input_counter_lock);
        memset(&audio_input_state.snapshot, 0, sizeof(audio_input_state.snapshot));
        audio_input_state.snapshot.is_running = true;
        portEXIT_CRITICAL(&audio_input_counter_lock);

        // Drain any stale completion token before the task that will post it.
        (void)xSemaphoreTake(audio_input_state.task_exit_semaphore, 0);
        audio_input_state.is_running = true;
        if (xTaskCreate(
                audio_input_capture_task,
                "audio_input",
                CONFIG_AUDIO_INPUT_TASK_STACK_SIZE,
                NULL,
                CONFIG_AUDIO_INPUT_TASK_PRIORITY,
                &audio_input_state.task_handle
            ) != pdPASS) {
            audio_input_state.is_running = false;
            audio_input_state.task_handle = NULL;
            portENTER_CRITICAL(&audio_input_counter_lock);
            audio_input_state.snapshot.is_running = false;
            portEXIT_CRITICAL(&audio_input_counter_lock);
            result = ESP_ERR_NO_MEM;
        } else {
            result = audio_pipeline_set_reference_sink(
                audio_input_reference_sink,
                &audio_input_state
            );
            if (result == ESP_OK) {
                result = audio_pipeline_capture_acquire(
                    AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
                );
            }
            if (result != ESP_OK) {
                audio_input_state.is_running = false;
                // Release is safe after a failed acquire: it can only succeed
                // when this module actually owns the lease.
                (void)audio_pipeline_capture_release(
                    AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
                );
                (void)audio_pipeline_set_reference_sink(NULL, NULL);
                (void)xSemaphoreTake(
                    audio_input_state.task_exit_semaphore,
                    pdMS_TO_TICKS(1000)
                );
                audio_input_state.task_handle = NULL;
                portENTER_CRITICAL(&audio_input_counter_lock);
                audio_input_state.snapshot.is_running = false;
                portEXIT_CRITICAL(&audio_input_counter_lock);
            }
        }
    }

    xSemaphoreGive(audio_input_state.lifecycle_mutex);
    return result;
}

static esp_err_t audio_input_stop_locked(void) {
    if (!audio_input_state.initialized || !audio_input_state.is_running) {
        return ESP_OK;
    }
    audio_input_state.is_running = false;
    portENTER_CRITICAL(&audio_input_counter_lock);
    audio_input_state.snapshot.is_running = false;
    portEXIT_CRITICAL(&audio_input_counter_lock);

    // Stop capturing first so the blocked I2S read returns and the task can
    // observe the cleared running flag.
    (void)audio_pipeline_capture_release(
        AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
    );
    (void)audio_pipeline_set_reference_sink(NULL, NULL);

    esp_err_t result = ESP_OK;
    if (audio_input_state.task_handle != NULL) {
        if (xSemaphoreTake(
                audio_input_state.task_exit_semaphore,
                pdMS_TO_TICKS(1000)
            ) != pdTRUE) {
            ESP_LOGE(TAG, "capture task did not stop in time");
            result = ESP_ERR_TIMEOUT;
        }
    }
    audio_input_state.task_handle = NULL;
    audio_input_state.callback = NULL;
    audio_input_state.metrics_callback = NULL;
    audio_input_state.callback_context = NULL;
    audio_input_reference_queue_flush(&audio_input_state.reference_queue);
    return result;
}

esp_err_t audio_input_stop(void) {
    if (audio_input_state.lifecycle_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(audio_input_state.lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = audio_input_stop_locked();
    xSemaphoreGive(audio_input_state.lifecycle_mutex);
    return result;
}

esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&audio_input_counter_lock);
    *snapshot_out = audio_input_state.snapshot;
    portEXIT_CRITICAL(&audio_input_counter_lock);
    return ESP_OK;
}

void audio_input_shutdown(void) {
    if (audio_input_state.lifecycle_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(audio_input_state.lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    (void)audio_input_stop_locked();
    if (audio_input_state.initialized) {
        audio_input_dsp_deinit(&audio_input_state.dsp);
        if (audio_input_state.reference_queue.mutex != NULL) {
            vSemaphoreDelete(audio_input_state.reference_queue.mutex);
            audio_input_state.reference_queue.mutex = NULL;
        }
        if (audio_input_state.task_exit_semaphore != NULL) {
            vSemaphoreDelete(audio_input_state.task_exit_semaphore);
            audio_input_state.task_exit_semaphore = NULL;
        }
        audio_input_state.initialized = false;
    }
    xSemaphoreGive(audio_input_state.lifecycle_mutex);
}

const module_descriptor_t *audio_input_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "audio_input",
        .version = "1.0.0",
        .initialize = audio_input_init,
        .shutdown = audio_input_shutdown,
    };
    return &descriptor;
}
