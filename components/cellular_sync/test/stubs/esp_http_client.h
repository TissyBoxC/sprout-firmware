#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_http_client *esp_http_client_handle_t;

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST = 1,
} esp_http_client_method_t;

typedef enum {
    HTTP_EVENT_ON_DATA = 0,
} esp_http_client_event_id_t;

typedef struct {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client;
    void *data;
    int data_len;
    void *user_data;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *event);

typedef struct {
    const char *url;
    esp_http_client_method_t method;
    int timeout_ms;
    http_event_handle_cb event_handler;
    void *user_data;
    int (*crt_bundle_attach)(void *conf);
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(
    const esp_http_client_config_t *config
);
esp_err_t esp_http_client_set_header(
    esp_http_client_handle_t client,
    const char *key,
    const char *value
);
esp_err_t esp_http_client_set_post_field(
    esp_http_client_handle_t client,
    const char *data,
    int len
);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);

#ifdef __cplusplus
}
#endif
