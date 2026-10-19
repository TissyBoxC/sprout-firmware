#include "esp_timer.h"
#include "esp_wifi.h"
#include "network_manager.h"

#include <stddef.h>

static network_manager_state_t host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
static network_manager_state_callback_t host_network_callback;
static esp_err_t host_ap_info_result = ESP_OK;
static int8_t host_rssi = -60;
static int host_timer_storage;
static esp_timer_cb_t host_timer_callback;
static int host_timer_starts;

network_manager_state_t network_manager_get_state(void) {
    return host_network_state;
}

esp_err_t network_manager_add_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    (void)context;
    host_network_callback = callback;
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *access_point) {
    if (host_ap_info_result != ESP_OK) {
        return host_ap_info_result;
    }
    if (access_point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    access_point->rssi = host_rssi;
    return ESP_OK;
}

esp_err_t esp_timer_create(
    const esp_timer_create_args_t *args,
    esp_timer_handle_t *handle_out
) {
    if (args == NULL || handle_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    host_timer_callback = args->callback;
    *handle_out = (esp_timer_handle_t)&host_timer_storage;
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t period_us) {
    (void)handle;
    (void)period_us;
    ++host_timer_starts;
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

// Test hooks.
void network_quality_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void network_quality_host_emit_network_state(network_manager_state_t state) {
    host_network_state = state;
    if (host_network_callback != NULL) {
        host_network_callback(state, NULL);
    }
}

void network_quality_host_set_ap_info_result(esp_err_t result) {
    host_ap_info_result = result;
}

void network_quality_host_set_rssi(int8_t rssi) {
    host_rssi = rssi;
}

void network_quality_host_run_timer(void) {
    if (host_timer_callback != NULL) {
        host_timer_callback(NULL);
    }
}
