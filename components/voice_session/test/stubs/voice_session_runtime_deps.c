#include "audio_codec.h"
#include "audio_input.h"
#include "audio_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "playback_queue.h"
#include "voice_wake.h"
#include "esp_websocket_client.h"


#include <string.h>

typedef struct {
    int length;
    int count;
    int head;
    int tail;
    unsigned char items[16][1024];
} host_queue_t;

typedef struct {
    bool active;
    int tokens;
} host_mutex_t;

static host_mutex_t host_mutex;
static host_queue_t host_control_queue;
static host_queue_t host_audio_queue;
static host_queue_t host_task_handle;
static voice_wake_snapshot_t host_wake_snapshot;
static int host_websocket_client;

void voice_session_test_set_wake_snapshot(
    const voice_wake_snapshot_t *snapshot
) {
    if (snapshot == NULL) {
        memset(&host_wake_snapshot, 0, sizeof(host_wake_snapshot));
        return;
    }
    host_wake_snapshot = *snapshot;
}

void voice_session_test_reset_queues(void) {
    memset(&host_control_queue, 0, sizeof(host_control_queue));
    memset(&host_audio_queue, 0, sizeof(host_audio_queue));
}

QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size) {
    if (length == 0) {
        return NULL;
    }
    host_queue_t *queue = length == 8
        ? &host_control_queue
        : &host_audio_queue;
    memset(queue, 0, sizeof(*queue));
    queue->length = (int)item_size;
    return (QueueHandle_t)queue;
}

BaseType_t xQueueSend(
    QueueHandle_t queue,
    const void *item,
    TickType_t ticks
) {
    (void)ticks;
    host_queue_t *state = (host_queue_t *)queue;
    if (state == NULL || item == NULL || state->count >= 16) {
        return pdFALSE;
    }
    if (state->length <= 0 || state->length > 1024) {
        return pdFALSE;
    }
    memcpy(state->items[state->tail], item, (size_t)state->length);
    state->tail = (state->tail + 1) % 16;
    state->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(
    QueueHandle_t queue,
    void *item_out,
    TickType_t ticks
) {
    (void)ticks;
    host_queue_t *state = (host_queue_t *)queue;
    if (state == NULL || item_out == NULL || state->count <= 0) {
        return pdFALSE;
    }
    if (state->length <= 0 || state->length > 1024) {
        return pdFALSE;
    }
    memcpy(item_out, state->items[state->head], (size_t)state->length);
    state->head = (state->head + 1) % 16;
    state->count--;
    return pdTRUE;
}

void vQueueDelete(QueueHandle_t queue) {
    host_queue_t *state = (host_queue_t *)queue;
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    host_mutex.active = true;
    host_mutex.tokens = 1;
    return &host_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    (void)ticks;
    host_mutex_t *state = (host_mutex_t *)semaphore;
    if (state == NULL || state->tokens <= 0) {
        return pdFALSE;
    }
    state->tokens--;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    host_mutex_t *state = (host_mutex_t *)semaphore;
    if (state == NULL) {
        return pdFALSE;
    }
    state->tokens++;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    host_mutex_t *state = (host_mutex_t *)semaphore;
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_depth,
    void *parameter,
    unsigned int priority,
    TaskHandle_t *handle_out
) {
    (void)task;
    (void)name;
    (void)stack_depth;
    (void)parameter;
    (void)priority;
    if (handle_out != NULL) {
        *handle_out = &host_task_handle;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

bool audio_codec_is_ready(void) {
    return true;
}

audio_codec_error_t audio_codec_decode_packet(
    const uint8_t *packet,
    size_t packet_size,
    audio_codec_pcm_frame_t *frame_out
) {
    (void)packet;
    (void)packet_size;
    (void)frame_out;
    return AUDIO_CODEC_ERR_DECODE_FAILED;
}

esp_err_t audio_input_start(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    void *context
) {
    (void)stream_id;
    (void)callback;
    (void)context;
    return ESP_OK;
}

esp_err_t audio_input_start_with_metrics(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    audio_input_metrics_callback_t metrics_callback,
    void *context
) {
    (void)stream_id;
    (void)callback;
    (void)metrics_callback;
    (void)context;
    return ESP_OK;
}

esp_err_t audio_input_stop(void) {
    return ESP_OK;
}

esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    return ESP_OK;
}

bool audio_pipeline_is_playing(void) {
    return false;
}

bool playback_queue_is_ready(void) {
    return true;
}

playback_queue_error_t playback_queue_enqueue(
    const playback_queue_item_t *item
) {
    (void)item;
    return PLAYBACK_QUEUE_OK;
}

esp_err_t playback_queue_clear(bool include_safety) {
    (void)include_safety;
    return ESP_OK;
}

network_manager_state_t network_manager_get_state(void) {
    return NETWORK_MANAGER_STATE_CONNECTED;
}

esp_err_t voice_wake_get_snapshot(voice_wake_snapshot_t *snapshot_out) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *snapshot_out = host_wake_snapshot;
    return ESP_OK;
}

esp_err_t voice_wake_suspend(void) {
    return ESP_OK;
}

esp_err_t voice_wake_resume(void) {
    return ESP_OK;
}

esp_websocket_client_handle_t esp_websocket_client_init(
    const esp_websocket_client_config_t *config
) {
    (void)config;
    return &host_websocket_client;
}

esp_err_t esp_websocket_register_events(
    esp_websocket_client_handle_t client,
    int32_t event_id,
    esp_websocket_event_handler_t handler,
    void *handler_args
) {
    (void)client;
    (void)event_id;
    (void)handler;
    (void)handler_args;
    return ESP_OK;
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client) {
    (void)client;
    return true;
}

int esp_websocket_client_send_text(
    esp_websocket_client_handle_t client,
    const char *data,
    int length,
    int timeout_ms
) {
    (void)client;
    (void)timeout_ms;
    (void)data;
    return length;
}

int esp_websocket_client_send_bin(
    esp_websocket_client_handle_t client,
    const char *data,
    int length,
    int timeout_ms
) {
    (void)client;
    (void)data;
    (void)timeout_ms;
    return length;
}

esp_err_t esp_websocket_client_close(
    esp_websocket_client_handle_t client,
    int timeout_ms
) {
    (void)client;
    (void)timeout_ms;
    return ESP_OK;
}

esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

int64_t esp_timer_get_time(void) {
    return 0;
}

uint32_t esp_random(void) {
    return 1;
}
