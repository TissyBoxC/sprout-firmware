#include "network_quality.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "network_manager.h"

#define NETWORK_QUALITY_INTERVAL_MS 30000
#define NETWORK_QUALITY_MINIMUM_RSSI (-127)

static bool network_quality_ready;
static esp_timer_handle_t network_quality_timer;
static network_quality_snapshot_t network_quality_snapshot = {
    .level = NETWORK_QUALITY_UNKNOWN,
    .rssi_dbm = 0,
    .latency_ms = 0,
    .packet_loss_percent = 0,
    .has_measurement = false,
};

static network_quality_level_t network_quality_classify(int rssi_dbm) {
    if (rssi_dbm <= CONFIG_NETWORK_QUALITY_POOR_RSSI) {
        return NETWORK_QUALITY_POOR;
    }
    if (rssi_dbm <= CONFIG_NETWORK_QUALITY_GOOD_RSSI) {
        return NETWORK_QUALITY_FAIR;
    }
    if (rssi_dbm <= CONFIG_NETWORK_QUALITY_EXCELLENT_RSSI) {
        return NETWORK_QUALITY_GOOD;
    }
    return NETWORK_QUALITY_EXCELLENT;
}

static void network_quality_update(void) {
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        network_quality_snapshot.level = NETWORK_QUALITY_UNKNOWN;
        network_quality_snapshot.has_measurement = false;
        return;
    }

    wifi_ap_record_t access_point = {0};
    const esp_err_t result = esp_wifi_sta_get_ap_info(&access_point);
    if (result != ESP_OK) {
        network_quality_snapshot.level = NETWORK_QUALITY_UNKNOWN;
        network_quality_snapshot.has_measurement = false;
        return;
    }
    const int rssi_dbm = (int)access_point.rssi;
    if (rssi_dbm < NETWORK_QUALITY_MINIMUM_RSSI || rssi_dbm > 0) {
        network_quality_snapshot.level = NETWORK_QUALITY_UNKNOWN;
        network_quality_snapshot.has_measurement = false;
        return;
    }
    network_quality_snapshot.rssi_dbm = rssi_dbm;
    network_quality_snapshot.level = network_quality_classify(rssi_dbm);
    // The ESP32 Wi-Fi driver exposes RSSI directly but not an application-level
    // RTT or loss counter. Keep the contract fields bounded and honest rather
    // than inventing a value the hardware cannot provide.
    network_quality_snapshot.latency_ms = 0;
    network_quality_snapshot.packet_loss_percent = 0;
    network_quality_snapshot.has_measurement = true;
}

static void network_quality_timer_callback(void *argument) {
    (void)argument;
    network_quality_update();
}

static void network_quality_network_callback(
    network_manager_state_t state,
    void *context
) {
    (void)context;
    if (state == NETWORK_MANAGER_STATE_CONNECTED) {
        network_quality_update();
        return;
    }
    network_quality_snapshot.level = NETWORK_QUALITY_UNKNOWN;
    network_quality_snapshot.has_measurement = false;
}

esp_err_t network_quality_init(void) {
    if (network_quality_ready) {
        return ESP_OK;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = network_quality_timer_callback,
        .name = "sprout_net_quality",
    };
    esp_err_t result = esp_timer_create(&timer_args, &network_quality_timer);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_timer_start_periodic(
        network_quality_timer,
        NETWORK_QUALITY_INTERVAL_MS * 1000
    );
    if (result != ESP_OK) {
        esp_timer_delete(network_quality_timer);
        network_quality_timer = NULL;
        return result;
    }
    result = network_manager_add_state_callback(
        network_quality_network_callback,
        NULL
    );
    if (result != ESP_OK) {
        esp_timer_stop(network_quality_timer);
        esp_timer_delete(network_quality_timer);
        network_quality_timer = NULL;
        return result;
    }
    network_quality_ready = true;
    return ESP_OK;
}

network_quality_snapshot_t network_quality_get_snapshot(void) {
    return network_quality_snapshot;
}

const char *network_quality_level_name(network_quality_level_t level) {
    switch (level) {
        case NETWORK_QUALITY_POOR:
            return "poor";
        case NETWORK_QUALITY_FAIR:
            return "fair";
        case NETWORK_QUALITY_GOOD:
            return "good";
        case NETWORK_QUALITY_EXCELLENT:
            return "excellent";
        case NETWORK_QUALITY_UNKNOWN:
        default:
            return "unknown";
    }
}

const module_descriptor_t *network_quality_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "network_quality",
        .version = "1.0.0",
        .initialize = network_quality_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
