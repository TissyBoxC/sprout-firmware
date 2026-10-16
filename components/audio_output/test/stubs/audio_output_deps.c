#include "audio_pipeline.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "volume_control.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    SemaphoreHandle_t handle;
    TickType_t tokens;
    TickType_t available;
} semaphore_state_t;

static semaphore_state_t semaphores[16];
static size_t semaphore_count;
static semaphore_state_t *signal_semaphore;
static semaphore_state_t *task_exit_semaphore;
static int binary_semaphore_count;
static TaskFunction_t task_function;
static void *task_parameter;

static bool host_pipeline_ready;
static bool host_pipeline_playing;
static audio_codec_error_t host_play_result;
static esp_err_t host_set_playing_result = ESP_OK;
static int host_play_calls;
static audio_codec_pcm_frame_t host_last_frame;
static bool host_has_last_frame;
static int host_task_create_calls;
static int host_fail_alloc_after = -1;
static int host_alloc_calls;
static uint8_t host_volume_percent = 100;
static uint8_t host_volume_max_percent = 100;
static bool host_volume_ready;
static bool host_volume_muted;
static int host_create_mutex_result = 1;
static int host_create_binary_result = 1;
static int host_task_create_result = 1;
static TickType_t host_tick_count;

void audio_output_host_reset(void);
void audio_output_host_set_pipeline_ready(bool ready);
void audio_output_host_set_pipeline_playing(bool playing);
void audio_output_host_set_set_playing_result(esp_err_t result);
void audio_output_host_set_play_result(audio_codec_error_t result);
void audio_output_host_set_create_mutex_result(int ok);
void audio_output_host_set_create_binary_result(int ok);
void audio_output_host_set_task_create_result(int ok);
void audio_output_host_fail_alloc_after(int successful_allocations);
void audio_output_host_set_volume(
    bool ready,
    uint8_t percent,
    uint8_t maximum_percent,
    bool muted
);
void audio_output_host_set_tick_count(TickType_t ticks);
void audio_output_host_advance_ticks(TickType_t delta);
int audio_output_host_play_calls(void);
bool audio_output_host_last_frame(audio_codec_pcm_frame_t *frame_out);
int audio_output_host_task_create_calls(void);
int audio_output_host_alloc_calls(void);
int audio_output_host_semaphore_count(void);
int audio_output_host_signal_tokens(void);
void audio_output_host_clear_signal_tokens(void);
void audio_output_host_complete_task(void);
void audio_output_host_run_task_once(void);

void audio_output_host_reset(void) {
    memset(semaphores, 0, sizeof(semaphores));
    semaphore_count = 0;
    signal_semaphore = NULL;
    task_exit_semaphore = NULL;
    binary_semaphore_count = 0;
    task_function = NULL;
    task_parameter = NULL;
    host_pipeline_ready = true;
    host_pipeline_playing = false;
    host_play_result = AUDIO_CODEC_OK;
    host_set_playing_result = ESP_OK;
    host_play_calls = 0;
    memset(&host_last_frame, 0, sizeof(host_last_frame));
    host_has_last_frame = false;
    host_task_create_calls = 0;
    host_fail_alloc_after = -1;
    host_alloc_calls = 0;
    host_volume_percent = 100;
    host_volume_max_percent = 100;
    host_volume_ready = false;
    host_volume_muted = false;
    host_create_mutex_result = 1;
    host_create_binary_result = 1;
    host_task_create_result = 1;
    host_tick_count = 0;
}

void audio_output_host_set_pipeline_ready(bool ready) {
    host_pipeline_ready = ready;
}

void audio_output_host_set_pipeline_playing(bool playing) {
    host_pipeline_playing = playing;
}

void audio_output_host_set_set_playing_result(esp_err_t result) {
    host_set_playing_result = result;
}

void audio_output_host_set_play_result(audio_codec_error_t result) {
    host_play_result = result;
}

void audio_output_host_set_create_mutex_result(int ok) {
    host_create_mutex_result = ok;
}

void audio_output_host_set_create_binary_result(int ok) {
    host_create_binary_result = ok;
}

void audio_output_host_set_task_create_result(int ok) {
    host_task_create_result = ok;
}

void audio_output_host_fail_alloc_after(int successful_allocations) {
    host_fail_alloc_after = successful_allocations;
}

void audio_output_host_set_volume(
    bool ready,
    uint8_t percent,
    uint8_t maximum_percent,
    bool muted
) {
    host_volume_ready = ready;
    host_volume_percent = percent;
    host_volume_max_percent = maximum_percent;
    host_volume_muted = muted;
}

void audio_output_host_set_tick_count(TickType_t ticks) {
    host_tick_count = ticks;
}

void audio_output_host_advance_ticks(TickType_t delta) {
    host_tick_count += delta;
}

int audio_output_host_play_calls(void) {
    return host_play_calls;
}

bool audio_output_host_last_frame(audio_codec_pcm_frame_t *frame_out) {
    if (frame_out == NULL || !host_has_last_frame) {
        return false;
    }
    *frame_out = host_last_frame;
    return true;
}

int audio_output_host_task_create_calls(void) {
    return host_task_create_calls;
}

int audio_output_host_alloc_calls(void) {
    return host_alloc_calls;
}

int audio_output_host_semaphore_count(void) {
    return (int)semaphore_count;
}

int audio_output_host_signal_tokens(void) {
    if (signal_semaphore == NULL) {
        return 0;
    }
    return (int)signal_semaphore->tokens;
}

void audio_output_host_clear_signal_tokens(void) {
    if (signal_semaphore != NULL) {
        signal_semaphore->tokens = 0;
    }
}

void audio_output_host_complete_task(void) {
    if (task_exit_semaphore != NULL) {
        task_exit_semaphore->tokens++;
    }
}

void audio_output_host_run_task_once(void) {
    if (task_function != NULL) {
        task_function(task_parameter);
    }
}

void *heap_caps_malloc_prefer(
    size_t size,
    size_t num,
    unsigned int caps1,
    unsigned int caps2
) {
    (void)num;
    (void)caps1;
    (void)caps2;
    host_alloc_calls++;
    if (host_fail_alloc_after >= 0 && host_alloc_calls > host_fail_alloc_after) {
        return NULL;
    }
    return malloc(size);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (!host_create_mutex_result) {
        return NULL;
    }
    semaphores[semaphore_count].handle = &semaphores[semaphore_count];
    semaphores[semaphore_count].tokens = 1;
    semaphores[semaphore_count].available = 1;
    semaphore_count++;
    return semaphores[semaphore_count - 1].handle;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (!host_create_binary_result) {
        return NULL;
    }
    semaphores[semaphore_count].handle = &semaphores[semaphore_count];
    semaphores[semaphore_count].tokens = 0;
    semaphores[semaphore_count].available = 1;
    semaphore_count++;
    semaphore_state_t *const state = &semaphores[semaphore_count - 1];
    /*
     * init() creates the signal binary first and the task-exit binary second.
     * Remember them separately so audio_output_signal_task() can be modeled
     * without leaking a token into the exit wait.
     */
    if (binary_semaphore_count == 0) {
        signal_semaphore = state;
    } else if (binary_semaphore_count == 1) {
        task_exit_semaphore = state;
    }
    binary_semaphore_count++;
    return (SemaphoreHandle_t)state;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    (void)ticks;
    semaphore_state_t *state = (semaphore_state_t *)semaphore;
    if (state == NULL || state->tokens == 0) {
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
    task_function = task;
    task_parameter = parameter;
    if (handle_out != NULL) {
        *handle_out = (TaskHandle_t)&task_function;
    }
    /*
     * Host tests do not run a scheduler. Mark the exit binary as ready once
     * the task has been registered so a later shutdown can model the task
     * observing audio_output_running == false and exiting.
     */
    if (task_exit_semaphore != NULL) {
        task_exit_semaphore->tokens++;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    host_tick_count += ticks;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

TickType_t xTaskGetTickCount(void) {
    return host_tick_count;
}

bool audio_pipeline_is_ready(void) {
    return host_pipeline_ready;
}

bool audio_pipeline_is_playing(void) {
    return host_pipeline_playing;
}

esp_err_t audio_pipeline_set_playing(bool playing) {
    host_pipeline_playing = playing;
    return host_set_playing_result;
}

audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
) {
    if (frame != NULL) {
        host_last_frame = *frame;
        host_has_last_frame = true;
    }
    host_play_calls++;
    return host_play_result;
}

bool volume_control_is_ready(void) {
    return host_volume_ready;
}

uint8_t volume_control_get_percent(void) {
    return host_volume_percent;
}

uint8_t volume_control_get_max_percent(void) {
    return host_volume_max_percent;
}

bool volume_control_is_muted(void) {
    return host_volume_muted;
}
