#include "voice_wake.h"

#include "audio_pipeline.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int tokens;
} semaphore_state_t;

static semaphore_state_t host_mutex;
static semaphore_state_t host_binary;
static TaskFunction_t host_task_function;
static void *host_task_parameter;
static TaskHandle_t host_task_handle;
static TickType_t host_ticks;
static int64_t host_now_us;

static bool host_pipeline_ready;
static bool host_pipeline_capturing;
static bool host_pipeline_playing;
static audio_pipeline_capture_owner_t host_capture_owner;
static audio_codec_pcm_frame_t host_next_frame;
static audio_codec_error_t host_capture_result;
static int host_capture_calls;

static int host_create_mutex_result = 1;
static int host_create_binary_result = 1;
static int host_task_create_result = 1;
static int host_task_create_calls;
static int host_signal_tokens;

static void host_generate_speech_like_frame(void);

void voice_wake_host_reset(void);
void voice_wake_host_set_pipeline_ready(bool ready);
void voice_wake_host_set_pipeline_playing(bool playing);
void voice_wake_host_set_pipeline_capturing(bool capturing);
void voice_wake_host_set_capture_result(audio_codec_error_t result);
void voice_wake_host_set_create_mutex_result(int ok);
void voice_wake_host_set_create_binary_result(int ok);
void voice_wake_host_set_task_create_result(int ok);
void voice_wake_host_set_now_ms(uint64_t now_ms);
void voice_wake_host_advance_ms(uint64_t delta_ms);
void voice_wake_host_set_signal_tokens(int tokens);
int voice_wake_host_signal_tokens(void);
int voice_wake_host_task_create_calls(void);
int voice_wake_host_capture_calls(void);
bool voice_wake_host_task_registered(void);
audio_pipeline_capture_owner_t voice_wake_host_capture_owner(void);
void voice_wake_host_run_task_once(void);
void voice_wake_host_complete_task(void);
void voice_wake_host_set_frame_value(int16_t value, uint32_t sequence);
void voice_wake_host_process_one_frame(void);

void voice_wake_host_reset(void) {
    memset(&host_mutex, 0, sizeof(host_mutex));
    memset(&host_binary, 0, sizeof(host_binary));
    host_mutex.tokens = 1;
    host_task_function = NULL;
    host_task_parameter = NULL;
    host_task_handle = NULL;
    host_ticks = 0;
    host_now_us = 0;
    host_pipeline_ready = true;
    host_pipeline_capturing = false;
    host_pipeline_playing = false;
    host_capture_owner = AUDIO_PIPELINE_CAPTURE_OWNER_NONE;
    memset(&host_next_frame, 0, sizeof(host_next_frame));
    host_capture_result = AUDIO_CODEC_OK;
    host_capture_calls = 0;
    host_create_mutex_result = 1;
    host_create_binary_result = 1;
    host_task_create_result = 1;
    host_task_create_calls = 0;
    host_signal_tokens = 0;
    host_generate_speech_like_frame();
}

void voice_wake_host_set_pipeline_ready(bool ready) {
    host_pipeline_ready = ready;
}

void voice_wake_host_set_pipeline_playing(bool playing) {
    host_pipeline_playing = playing;
}

void voice_wake_host_set_pipeline_capturing(bool capturing) {
    host_pipeline_capturing = capturing;
}

void voice_wake_host_set_capture_result(audio_codec_error_t result) {
    host_capture_result = result;
}

void voice_wake_host_set_create_mutex_result(int ok) {
    host_create_mutex_result = ok;
}

void voice_wake_host_set_create_binary_result(int ok) {
    host_create_binary_result = ok;
}

void voice_wake_host_set_task_create_result(int ok) {
    host_task_create_result = ok;
}

void voice_wake_host_set_now_ms(uint64_t now_ms) {
    host_now_us = (int64_t)(now_ms * 1000u);
}

void voice_wake_host_advance_ms(uint64_t delta_ms) {
    host_now_us += (int64_t)(delta_ms * 1000u);
}

void voice_wake_host_set_signal_tokens(int tokens) {
    host_signal_tokens = tokens;
}

int voice_wake_host_signal_tokens(void) {
    return host_signal_tokens;
}

int voice_wake_host_task_create_calls(void) {
    return host_task_create_calls;
}

int voice_wake_host_capture_calls(void) {
    return host_capture_calls;
}

bool voice_wake_host_task_registered(void) {
    return host_task_function != NULL;
}

audio_pipeline_capture_owner_t voice_wake_host_capture_owner(void) {
    return host_capture_owner;
}

void voice_wake_host_run_task_once(void) {
    (void)host_task_function;
    (void)host_task_parameter;
    voice_wake_host_process_one_frame();
}

void voice_wake_host_complete_task(void) {
    host_binary.tokens++;
}

void voice_wake_host_set_frame_value(int16_t value, uint32_t sequence) {
    memset(&host_next_frame, 0, sizeof(host_next_frame));
    host_next_frame.sequence = sequence;
    host_next_frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    for (size_t index = 0; index < AUDIO_CODEC_SAMPLES_PER_FRAME; ++index) {
        host_next_frame.pcm[index] =
            ((index / 20u) % 2u == 0u) ? value : (int16_t)-value;
    }
}

static void host_generate_speech_like_frame(void) {
    for (size_t index = 0; index < AUDIO_CODEC_SAMPLES_PER_FRAME; ++index) {
        host_next_frame.pcm[index] =
            ((index / 20u) % 2u == 0u) ? 2000 : -2000;
    }
    host_next_frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
}

int64_t esp_timer_get_time(void) {
    return host_now_us;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (!host_create_mutex_result) {
        return NULL;
    }
    return &host_mutex;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (!host_create_binary_result) {
        return NULL;
    }
    host_signal_tokens = 0;
    return &host_binary;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    (void)ticks;
    semaphore_state_t *state = (semaphore_state_t *)semaphore;
    if (state == NULL || state->tokens <= 0) {
        return pdFALSE;
    }
    state->tokens--;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    semaphore_state_t *state = (semaphore_state_t *)semaphore;
    if (state == NULL) {
        return pdFALSE;
    }
    state->tokens++;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_depth,
    void *parameter,
    unsigned int priority,
    TaskHandle_t *handle_out
) {
    (void)name;
    (void)stack_depth;
    (void)priority;
    host_task_create_calls++;
    if (!host_task_create_result) {
        return pdFALSE;
    }
    host_task_function = task;
    host_task_parameter = parameter;
    host_task_handle = (TaskHandle_t)&host_task_function;
    if (handle_out != NULL) {
        *handle_out = host_task_handle;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    host_ticks += ticks;
    host_now_us += (int64_t)ticks * 1000;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

bool audio_pipeline_is_ready(void) {
    return host_pipeline_ready;
}

bool audio_pipeline_is_capturing(void) {
    return host_pipeline_capturing;
}

bool audio_pipeline_is_playing(void) {
    return host_pipeline_playing;
}

esp_err_t audio_pipeline_capture_acquire(
    audio_pipeline_capture_owner_t owner
) {
    if (host_capture_owner != AUDIO_PIPELINE_CAPTURE_OWNER_NONE &&
        host_capture_owner != owner) {
        return ESP_ERR_INVALID_STATE;
    }
    host_capture_owner = owner;
    host_pipeline_capturing = true;
    return ESP_OK;
}

esp_err_t audio_pipeline_capture_release(
    audio_pipeline_capture_owner_t owner
) {
    if (host_capture_owner != owner) {
        return ESP_ERR_INVALID_STATE;
    }
    host_capture_owner = AUDIO_PIPELINE_CAPTURE_OWNER_NONE;
    host_pipeline_capturing = false;
    return ESP_OK;
}

audio_codec_error_t audio_pipeline_capture_frame(
    audio_pipeline_capture_owner_t owner,
    audio_codec_pcm_frame_t *frame_out
) {
    host_capture_calls++;
    if (owner != host_capture_owner || frame_out == NULL) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    if (host_capture_result != AUDIO_CODEC_OK) {
        return host_capture_result;
    }
    *frame_out = host_next_frame;
    return AUDIO_CODEC_OK;
}
