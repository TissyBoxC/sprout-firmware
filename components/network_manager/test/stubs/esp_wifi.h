#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
} wifi_mode_t;

typedef enum {
    WIFI_STORAGE_FLASH = 0,
    WIFI_STORAGE_RAM,
} wifi_storage_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA2_PSK,
} wifi_auth_mode_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP,
} wifi_interface_t;

typedef struct {
    uint8_t ssid[WIFI_SSID_MAX_LEN];
    uint8_t password[WIFI_PASSWORD_MAX_LEN];
    struct {
        wifi_auth_mode_t authmode;
    } threshold;
} wifi_sta_config_t;

typedef struct {
    wifi_sta_config_t sta;
} wifi_config_t;

typedef struct {
    uint8_t ssid[WIFI_SSID_MAX_LEN];
} wifi_ap_record_t;

typedef struct {
    int dummy;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_config(
    wifi_interface_t interface,
    const wifi_config_t *config
);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *access_point_record);
