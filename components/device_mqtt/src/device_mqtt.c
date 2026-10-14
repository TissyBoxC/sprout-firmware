#include "device_mqtt.h"

#include "esp_log.h"

static const char *const TAG = "device_mqtt";
static bool device_mqtt_ready;

esp_err_t device_mqtt_init(void) {
    if (device_mqtt_ready) {
        return ESP_OK;
    }
    device_mqtt_reset_state();
    device_mqtt_ready = true;
    ESP_LOGI(TAG, "device MQTT command contract ready");
    return ESP_OK;
}

bool device_mqtt_is_ready(void) {
    return device_mqtt_ready;
}

const module_descriptor_t *device_mqtt_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_mqtt",
        .version = "1.0.0",
        .initialize = device_mqtt_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
