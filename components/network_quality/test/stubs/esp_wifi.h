#pragma once

#include <stdint.h>

#include "esp_err.h"

// Only the field network_quality reads is modeled.
typedef struct {
    int8_t rssi;
} wifi_ap_record_t;

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *access_point);
