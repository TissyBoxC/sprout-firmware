#include "audio_pipeline.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdlib.h>

// Host stand-ins for the RTOS primitives and the fallback speaker path. The
// worker task is created but never scheduled, so enqueue/clear/pause semantics
// are observable without concurrency.
static bool host_create_mutex_fails;
static bool host_create_binary_fails;
static bool host_create_task_fails;
static bool host_pipeline_ready = true;
static esp_err_t host_set_playing_result = ESP_OK;
static audio_codec_error_t host_play_frame_result = AUDIO_CODEC_OK;

static int host_mutex;
static int host_signal;
static int host_task_exit;
static bool host_binary_handed_out_signal;

static int host_task_create_calls;
static int host_play_frame_calls;
static int host_alloc_fail_after = -1;
static int host_alloc_calls;

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return host_create_mutex_fails ? NULL : &host_mutex;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (host_create_binary_fails) {
        return NULL;
    }
    if (!host_binary_handed_out_signal) {
        host_binary_handed_out_signal = true;
        return &host_signal;
    }
    return &host_task_exit;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)semaphore;
    (void)timeout;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    UBaseType_t priority,
    TaskHandle_t *handle
) {
    (void)task;
    (void)name;
    (void)stack_size;
    (void)argument;
    (void)priority;
    ++host_task_create_calls;
    if (host_create_task_fails) {
        return pdFAIL;
    }
    // The worker never runs, so a recorded handle would make shutdown wait for
    // a task that cannot exit.
    if (handle != NULL) {
        *handle = NULL;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

bool audio_pipeline_is_ready(void) {
    return host_pipeline_ready;
}

esp_err_t audio_pipeline_set_playing(bool is_playing) {
    (void)is_playing;
    return host_set_playing_result;
}

audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
) {
    (void)frame;
    ++host_play_frame_calls;
    return host_play_frame_result;
}

void *heap_caps_malloc_prefer(
    size_t size,
    size_t prefer_count,
    int prefer,
    int fallback
) {
    (void)prefer_count;
    (void)prefer;
    (void)fallback;
    ++host_alloc_calls;
    if (host_alloc_fail_after >= 0 && host_alloc_calls > host_alloc_fail_after) {
        return NULL;
    }
    return malloc(size);
}

// Test hooks.
void playback_queue_host_reset(void) {
    host_create_mutex_fails = false;
    host_create_binary_fails = false;
    host_create_task_fails = false;
    host_pipeline_ready = true;
    host_set_playing_result = ESP_OK;
    host_play_frame_result = AUDIO_CODEC_OK;
    host_task_create_calls = 0;
    host_play_frame_calls = 0;
    host_alloc_fail_after = -1;
    host_alloc_calls = 0;
    host_binary_handed_out_signal = false;
}

void playback_queue_host_set_create_mutex_fails(bool fails) {
    host_create_mutex_fails = fails;
}

void playback_queue_host_set_create_binary_fails(bool fails) {
    host_create_binary_fails = fails;
}

void playback_queue_host_set_create_task_fails(bool fails) {
    host_create_task_fails = fails;
}

void playback_queue_host_set_pipeline_ready(bool ready) {
    host_pipeline_ready = ready;
}

void playback_queue_host_set_playing_result(esp_err_t result) {
    host_set_playing_result = result;
}

void playback_queue_host_set_play_frame_result(audio_codec_error_t result) {
    host_play_frame_result = result;
}

void playback_queue_host_fail_alloc_after(int successful_allocations) {
    host_alloc_fail_after = successful_allocations;
    host_alloc_calls = 0;
}

int playback_queue_host_task_create_calls(void) {
    return host_task_create_calls;
}

int playback_queue_host_play_frame_calls(void) {
    return host_play_frame_calls;
}
