#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(
    void *handler_arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
);

extern const char *WIFI_EVENT;
extern const char *IP_EVENT;

#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler,
    void *handler_arg,
    void **instance
);
