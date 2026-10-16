#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    NETWORK_PROV_START = 0,
    NETWORK_PROV_END,
    NETWORK_PROV_WIFI_CRED_RECV,
    NETWORK_PROV_WIFI_CRED_SUCCESS,
    NETWORK_PROV_WIFI_CRED_FAIL,
} network_prov_cb_event_t;

typedef enum {
    NETWORK_PROV_WIFI_STA_AUTH_ERROR = 0,
    NETWORK_PROV_WIFI_STA_DISCONNECTED,
} network_prov_wifi_sta_fail_reason_t;

typedef enum {
    NETWORK_PROV_SECURITY_2 = 0,
} network_prov_security_t;

typedef enum {
    NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM = 0,
} network_prov_scheme_event_handler_t;

typedef struct {
    char *salt;
    uint16_t salt_len;
    char *verifier;
    uint16_t verifier_len;
} network_prov_security2_params_t;

typedef void (*network_prov_event_cb_t)(
    void *user_data,
    network_prov_cb_event_t event,
    void *event_data
);

typedef struct {
    network_prov_event_cb_t event_cb;
} network_prov_app_event_handler_t;

typedef struct {
    const void *scheme;
    network_prov_scheme_event_handler_t scheme_event_handler;
    network_prov_app_event_handler_t app_event_handler;
} network_prov_mgr_config_t;

#ifdef __cplusplus
extern "C" {
#endif

extern const void *network_prov_scheme_ble;

esp_err_t network_prov_mgr_init(network_prov_mgr_config_t config);
esp_err_t network_prov_mgr_endpoint_create(const char *endpoint);
esp_err_t network_prov_mgr_disable_auto_stop(uint32_t timeout_ms);
esp_err_t network_prov_mgr_start_provisioning(
    network_prov_security_t security,
    const network_prov_security2_params_t *security2_params,
    const char *service_name,
    const char *service_key
);
esp_err_t network_prov_mgr_endpoint_register(
    const char *endpoint,
    esp_err_t (*handler)(
        uint32_t session_id,
        const uint8_t *input,
        ssize_t input_length,
        uint8_t **output,
        ssize_t *output_length,
        void *private_data
    ),
    void *user_data
);
esp_err_t network_prov_mgr_stop_provisioning(void);
void network_prov_mgr_wait(void);
esp_err_t network_prov_mgr_deinit(void);

#ifdef __cplusplus
}
#endif
