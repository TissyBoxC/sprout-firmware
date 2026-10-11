#include "temperature_monitor.h"

#include "esp_log.h"
#include "esp_timer.h"

#if __has_include("driver/temperature_sensor.h")
#include "driver/temperature_sensor.h"
#define TEMPERATURE_MONITOR_HAS_DRIVER 1
#else
#define TEMPERATURE_MONITOR_HAS_DRIVER 0
#endif

#include "temperature_monitor_core.h"

#define TEMPERATURE_MONITOR_INTERVAL_MS 60000

static const char *TAG = "sprout_temp";

static bool temperature_monitor_ready;
static esp_timer_handle_t temperature_monitor_timer;
static temperature_snapshot_t temperature_monitor_snapshot = {
    .state = TEMPERATURE_STATE_UNKNOWN,
    .celsius_tenths = 0,
    .has_measurement = false,
};

#if TEMPERATURE_MONITOR_HAS_DRIVER
static temperature_sensor_handle_t temperature_monitor_sensor;

static const temperature_core_thresholds_t temperature_monitor_thresholds = {
    .warm_tenths = CONFIG_TEMPERATURE_WARM_CELSIUS * 10,
    .hot_tenths = CONFIG_TEMPERATURE_HOT_CELSIUS * 10,
    .critical_tenths = CONFIG_TEMPERATURE_CRITICAL_CELSIUS * 10,
};
#endif

static temperature_state_t temperature_monitor_from_core(
    temperature_core_state_t core
) {
    switch (core) {
        case TEMPERATURE_CORE_NORMAL:
            return TEMPERATURE_STATE_NORMAL;
        case TEMPERATURE_CORE_WARM:
            return TEMPERATURE_STATE_WARM;
        case TEMPERATURE_CORE_HOT:
            return TEMPERATURE_STATE_HOT;
        case TEMPERATURE_CORE_CRITICAL:
            return TEMPERATURE_STATE_CRITICAL;
        case TEMPERATURE_CORE_UNKNOWN:
        default:
            return TEMPERATURE_STATE_UNKNOWN;
    }
}

static void temperature_monitor_update(void) {
#if TEMPERATURE_MONITOR_HAS_DRIVER
    if (temperature_monitor_sensor == NULL) {
        temperature_monitor_snapshot.state = TEMPERATURE_STATE_UNKNOWN;
        temperature_monitor_snapshot.has_measurement = false;
        return;
    }
    float celsius = 0.0f;
    const esp_err_t result = temperature_sensor_get_celsius(
        temperature_monitor_sensor,
        &celsius
    );
    if (result != ESP_OK) {
        temperature_monitor_snapshot.state = TEMPERATURE_STATE_UNKNOWN;
        temperature_monitor_snapshot.has_measurement = false;
        return;
    }
    // Round to the nearest tenth of a degree; the platform stores integer
    // tenths to keep the contract free of floating-point drift.
    const int tenths = (int)((celsius >= 0.0f ? 0.5f : -0.5f) + celsius * 10.0f);
    const temperature_core_state_t core = temperature_core_classify(
        tenths,
        &temperature_monitor_thresholds
    );
    if (core == TEMPERATURE_CORE_UNKNOWN) {
        temperature_monitor_snapshot.state = TEMPERATURE_STATE_UNKNOWN;
        temperature_monitor_snapshot.has_measurement = false;
        return;
    }
    temperature_monitor_snapshot.state = temperature_monitor_from_core(core);
    temperature_monitor_snapshot.celsius_tenths = tenths;
    temperature_monitor_snapshot.has_measurement = true;
#else
    temperature_monitor_snapshot.state = TEMPERATURE_STATE_UNKNOWN;
    temperature_monitor_snapshot.has_measurement = false;
#endif
}

static void temperature_monitor_timer_callback(void *argument) {
    (void)argument;
    temperature_monitor_update();
}

esp_err_t temperature_monitor_init(void) {
    if (temperature_monitor_ready) {
        return ESP_OK;
    }
#if TEMPERATURE_MONITOR_HAS_DRIVER
    const temperature_sensor_config_t sensor_config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(
            CONFIG_TEMPERATURE_MIN_CELSIUS,
            CONFIG_TEMPERATURE_MAX_CELSIUS
        );
    esp_err_t result = temperature_sensor_install(
        &sensor_config,
        &temperature_monitor_sensor
    );
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "temperature sensor unavailable: %s", esp_err_to_name(result));
        temperature_monitor_sensor = NULL;
    } else {
        result = temperature_sensor_enable(temperature_monitor_sensor);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "temperature sensor enable failed: %s", esp_err_to_name(result));
            temperature_sensor_uninstall(temperature_monitor_sensor);
            temperature_monitor_sensor = NULL;
        }
    }
#endif
    const esp_timer_create_args_t timer_args = {
        .callback = temperature_monitor_timer_callback,
        .name = "sprout_temp",
    };
    esp_err_t timer_result = esp_timer_create(
        &timer_args,
        &temperature_monitor_timer
    );
    if (timer_result != ESP_OK) {
        return timer_result;
    }
    timer_result = esp_timer_start_periodic(
        temperature_monitor_timer,
        TEMPERATURE_MONITOR_INTERVAL_MS * 1000
    );
    if (timer_result != ESP_OK) {
        esp_timer_delete(temperature_monitor_timer);
        temperature_monitor_timer = NULL;
        return timer_result;
    }
    temperature_monitor_update();
    temperature_monitor_ready = true;
    return ESP_OK;
}

temperature_snapshot_t temperature_monitor_get_snapshot(void) {
    return temperature_monitor_snapshot;
}

const char *temperature_monitor_state_name(temperature_state_t state) {
    return temperature_core_state_name(
        (temperature_core_state_t)state
    );
}

const module_descriptor_t *temperature_monitor_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "temperature_monitor",
        .version = "1.0.0",
        .initialize = temperature_monitor_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
