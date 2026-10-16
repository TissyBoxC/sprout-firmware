#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    WIFI_IF_STA = 0,
} wifi_interface_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
} wifi_sta_config_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_wifi_get_mac(wifi_interface_t interface, uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
