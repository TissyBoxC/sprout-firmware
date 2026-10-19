#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

typedef void *esp_websocket_client_handle_t;
typedef void (*esp_websocket_event_handler_t)(
    void *handler_args,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
);

typedef enum {
    WEBSOCKET_EVENT_ANY = -1,
    WEBSOCKET_EVENT_ERROR = 0,
    WEBSOCKET_EVENT_CONNECTED,
    WEBSOCKET_EVENT_DISCONNECTED,
    WEBSOCKET_EVENT_DATA,
    WEBSOCKET_EVENT_CLOSED,
} esp_websocket_event_id_t;

typedef struct {
    const char *uri;
    const char *headers;
    int crt_bundle_attach;
    int task_prio;
    int task_stack;
    int buffer_size;
    int reconnect_timeout_ms;
    int network_timeout_ms;
} esp_websocket_client_config_t;

typedef struct {
    int payload_offset;
    int data_len;
    uint8_t op_code;
    char *data_ptr;
} esp_websocket_event_data_t;

esp_websocket_client_handle_t esp_websocket_client_init(
    const esp_websocket_client_config_t *config
);
esp_err_t esp_websocket_register_events(
    esp_websocket_client_handle_t client,
    int32_t event_id,
    esp_websocket_event_handler_t handler,
    void *handler_args
);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client);
int esp_websocket_client_send_text(
    esp_websocket_client_handle_t client,
    const char *data,
    int length,
    int timeout_ms
);
int esp_websocket_client_send_bin(
    esp_websocket_client_handle_t client,
    const char *data,
    int length,
    int timeout_ms
);
esp_err_t esp_websocket_client_close(
    esp_websocket_client_handle_t client,
    int timeout_ms
);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);
