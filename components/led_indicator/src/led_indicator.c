#include "led_indicator.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "sdkconfig.h"

#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER && __has_include("diagnostic_reporter.h")
#include "diagnostic_reporter.h"
#define LED_INDICATOR_HAS_DIAGNOSTIC_REPORTER 1
#else
#define LED_INDICATOR_HAS_DIAGNOSTIC_REPORTER 0
#endif

// Kconfig omits a `default n` bool from sdkconfig.h, so an active-high build
// would fail to compile without an explicit fallback.
#ifndef CONFIG_LED_INDICATOR_ACTIVE_LOW
#define CONFIG_LED_INDICATOR_ACTIVE_LOW 0
#endif

static const char *const TAG = "led_indicator";

#define LED_INDICATOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define LED_INDICATOR_LEDC_TIMER LEDC_TIMER_0
#define LED_INDICATOR_LEDC_CHANNEL LEDC_CHANNEL_0
#define LED_INDICATOR_LEDC_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define LED_INDICATOR_LEDC_DUTY_MAX ((1U << 10) - 1U)
#define LED_INDICATOR_POLL_INTERVAL_TICKS \
    pdMS_TO_TICKS(CONFIG_LED_INDICATOR_POLL_INTERVAL_MS)
#define LED_INDICATOR_LEDC_IDLE_LEVEL \
    (CONFIG_LED_INDICATOR_ACTIVE_LOW ? 1U : 0U)

typedef enum {
    LED_INDICATOR_PATTERN_OFF = 0,
    LED_INDICATOR_PATTERN_SOLID,
    LED_INDICATOR_PATTERN_SLOW_BLINK,
    LED_INDICATOR_PATTERN_FAST_BLINK,
    LED_INDICATOR_PATTERN_BREATHING,
} led_indicator_pattern_t;

typedef struct {
    led_indicator_state_t state;
    led_indicator_pattern_t pattern;
    uint32_t on_ms;
    uint32_t off_ms;
} led_indicator_pattern_entry_t;

// Every state maps to a bounded pattern. Durations are intentionally short
// enough to keep the task responsive while still being visible to a child.
static const led_indicator_pattern_entry_t LED_INDICATOR_PATTERNS[] = {
    {LED_INDICATOR_STATE_OFF, LED_INDICATOR_PATTERN_OFF, 0U, 0U},
    {LED_INDICATOR_STATE_BOOTING, LED_INDICATOR_PATTERN_SLOW_BLINK, 250U, 250U},
    {LED_INDICATOR_STATE_PROVISIONING, LED_INDICATOR_PATTERN_SLOW_BLINK, 300U, 300U},
    {LED_INDICATOR_STATE_WIFI_CONNECTING, LED_INDICATOR_PATTERN_SLOW_BLINK, 500U, 500U},
    {LED_INDICATOR_STATE_CONNECTING, LED_INDICATOR_PATTERN_SLOW_BLINK, 400U, 400U},
    {LED_INDICATOR_STATE_IDLE, LED_INDICATOR_PATTERN_SOLID, 0U, 0U},
    {LED_INDICATOR_STATE_LISTENING, LED_INDICATOR_PATTERN_BREATHING, 1200U, 0U},
    {LED_INDICATOR_STATE_THINKING, LED_INDICATOR_PATTERN_BREATHING, 800U, 0U},
    {LED_INDICATOR_STATE_SPEAKING, LED_INDICATOR_PATTERN_SOLID, 0U, 0U},
    {LED_INDICATOR_STATE_MUTED, LED_INDICATOR_PATTERN_OFF, 0U, 0U},
    {LED_INDICATOR_STATE_ERROR, LED_INDICATOR_PATTERN_FAST_BLINK, 80U, 120U},
    {LED_INDICATOR_STATE_LOW_BATTERY, LED_INDICATOR_PATTERN_SLOW_BLINK, 120U, 1880U},
    {LED_INDICATOR_STATE_FACTORY_RESET, LED_INDICATOR_PATTERN_FAST_BLINK, 100U, 100U},
    {LED_INDICATOR_STATE_OTA_UPGRADING, LED_INDICATOR_PATTERN_SLOW_BLINK, 200U, 200U},
};

static SemaphoreHandle_t led_indicator_mutex;
static SemaphoreHandle_t led_indicator_signal;
static SemaphoreHandle_t led_indicator_task_exit;
static TaskHandle_t led_indicator_task_handle;
static volatile bool led_indicator_running;
static bool led_indicator_ready;
static bool led_indicator_enabled;
static led_indicator_state_t led_indicator_state;
static led_indicator_error_t led_indicator_last_error;
static uint32_t led_indicator_transitions;
static led_indicator_pattern_t led_indicator_active_pattern;
static led_indicator_state_t led_indicator_active_state;
static uint32_t led_indicator_pattern_started_ms;
static uint32_t led_indicator_pattern_phase_ms;

static bool led_indicator_state_is_valid(led_indicator_state_t state) {
    return state >= LED_INDICATOR_STATE_OFF && state < LED_INDICATOR_STATE_COUNT;
}

static const led_indicator_pattern_entry_t *led_indicator_pattern_for_state(
    led_indicator_state_t state
) {
    for (size_t index = 0;
         index < sizeof(LED_INDICATOR_PATTERNS) /
                     sizeof(LED_INDICATOR_PATTERNS[0]);
         ++index) {
        if (LED_INDICATOR_PATTERNS[index].state == state) {
            return &LED_INDICATOR_PATTERNS[index];
        }
    }
    return &LED_INDICATOR_PATTERNS[0];
}

static uint32_t led_indicator_scale_duty(uint32_t percent) {
    if (percent == 0U) {
        return 0U;
    }
    if (percent >= 100U) {
        return LED_INDICATOR_LEDC_DUTY_MAX;
    }
    return (LED_INDICATOR_LEDC_DUTY_MAX * percent) / 100U;
}

static uint32_t led_indicator_on_duty(void) {
    return led_indicator_scale_duty(CONFIG_LED_INDICATOR_BRIGHTNESS_PERCENT);
}

static esp_err_t led_indicator_write_duty_locked(uint32_t duty) {
    esp_err_t result = ledc_set_duty(
        LED_INDICATOR_LEDC_MODE,
        LED_INDICATOR_LEDC_CHANNEL,
        duty
    );
    if (result != ESP_OK) {
        return result;
    }
    return ledc_update_duty(
        LED_INDICATOR_LEDC_MODE,
        LED_INDICATOR_LEDC_CHANNEL
    );
}

static uint32_t led_indicator_phase_duty(
    const led_indicator_pattern_entry_t *pattern,
    uint32_t phase_ms
) {
    switch (pattern->pattern) {
        case LED_INDICATOR_PATTERN_OFF:
            return 0U;
        case LED_INDICATOR_PATTERN_SOLID:
            return led_indicator_on_duty();
        case LED_INDICATOR_PATTERN_SLOW_BLINK:
        case LED_INDICATOR_PATTERN_FAST_BLINK:
            if (pattern->on_ms == 0U || pattern->off_ms == 0U) {
                return led_indicator_on_duty();
            }
            return phase_ms < pattern->on_ms ? led_indicator_on_duty() : 0U;
        case LED_INDICATOR_PATTERN_BREATHING: {
            if (pattern->on_ms == 0U) {
                return 0U;
            }
            const uint32_t half_period_ms = pattern->on_ms;
            const uint32_t position_ms = phase_ms % (half_period_ms * 2U);
            uint32_t level;
            if (position_ms < half_period_ms) {
                level = position_ms;
            } else {
                level = (half_period_ms * 2U) - position_ms;
            }
            return led_indicator_scale_duty(
                (level * CONFIG_LED_INDICATOR_BRIGHTNESS_PERCENT) /
                half_period_ms
            );
        }
        default:
            return 0U;
    }
}

static uint32_t led_indicator_pattern_period_ms(
    const led_indicator_pattern_entry_t *pattern
) {
    switch (pattern->pattern) {
        case LED_INDICATOR_PATTERN_SLOW_BLINK:
        case LED_INDICATOR_PATTERN_FAST_BLINK:
            if (pattern->on_ms == 0U || pattern->off_ms == 0U) {
                return CONFIG_LED_INDICATOR_POLL_INTERVAL_MS;
            }
            return pattern->on_ms + pattern->off_ms;
        case LED_INDICATOR_PATTERN_BREATHING:
            if (pattern->on_ms == 0U) {
                return CONFIG_LED_INDICATOR_POLL_INTERVAL_MS;
            }
            return pattern->on_ms * 2U;
        default:
            return CONFIG_LED_INDICATOR_POLL_INTERVAL_MS;
    }
}

static esp_err_t led_indicator_apply_pattern_locked(
    led_indicator_state_t state,
    const led_indicator_pattern_entry_t *pattern,
    TickType_t now_ticks
) {
    led_indicator_active_state = state;
    led_indicator_active_pattern = pattern->pattern;
    led_indicator_pattern_started_ms = (uint32_t)(
        (uint64_t)now_ticks * portTICK_PERIOD_MS
    );
    led_indicator_pattern_phase_ms = 0U;
    return led_indicator_write_duty_locked(
        led_indicator_phase_duty(pattern, led_indicator_pattern_phase_ms)
    );
}

static void led_indicator_record_error_locked(esp_err_t error) {
    switch (error) {
        case ESP_ERR_INVALID_ARG:
        case ESP_ERR_INVALID_STATE:
            led_indicator_last_error = LED_INDICATOR_ERR_GPIO_UNAVAILABLE;
            break;
        case ESP_ERR_NO_MEM:
            led_indicator_last_error = LED_INDICATOR_ERR_NO_MEM;
            break;
        default:
            led_indicator_last_error = LED_INDICATOR_ERR_GPIO_UNAVAILABLE;
            break;
    }
}

static void led_indicator_record_error(esp_err_t error) {
    if (led_indicator_mutex != NULL &&
        xSemaphoreTake(led_indicator_mutex, portMAX_DELAY) == pdTRUE) {
        led_indicator_record_error_locked(error);
        xSemaphoreGive(led_indicator_mutex);
    }
}

static void led_indicator_report_state(led_indicator_state_t state) {
#if LED_INDICATOR_HAS_DIAGNOSTIC_REPORTER
    // The LED state is an instantaneous transition. Record the stable state
    // name instead of a duration or a free-form description.
    (void)diagnostic_reporter_record_interaction(
        "indicator_state",
        led_indicator_state_name(state),
        0
    );
#else
    (void)state;
#endif
}

static void led_indicator_deconfigure_ledc(void) {
    (void)ledc_stop(
        LED_INDICATOR_LEDC_MODE,
        LED_INDICATOR_LEDC_CHANNEL,
        LED_INDICATOR_LEDC_IDLE_LEVEL
    );
    (void)ledc_timer_pause(
        LED_INDICATOR_LEDC_MODE,
        LED_INDICATOR_LEDC_TIMER
    );

    const ledc_channel_config_t deconfigure_channel = {
        .speed_mode = LED_INDICATOR_LEDC_MODE,
        .channel = LED_INDICATOR_LEDC_CHANNEL,
        .deconfigure = true,
    };
    (void)ledc_channel_config(&deconfigure_channel);
    const ledc_timer_config_t deconfigure_timer = {
        .speed_mode = LED_INDICATOR_LEDC_MODE,
        .timer_num = LED_INDICATOR_LEDC_TIMER,
        .deconfigure = true,
    };
    (void)ledc_timer_config(&deconfigure_timer);
}

static void led_indicator_release_sync_objects(void) {
    if (led_indicator_mutex != NULL) {
        vSemaphoreDelete(led_indicator_mutex);
        led_indicator_mutex = NULL;
    }
    if (led_indicator_signal != NULL) {
        vSemaphoreDelete(led_indicator_signal);
        led_indicator_signal = NULL;
    }
    if (led_indicator_task_exit != NULL) {
        vSemaphoreDelete(led_indicator_task_exit);
        led_indicator_task_exit = NULL;
    }
}

static void led_indicator_task(void *argument) {
    (void)argument;

    while (led_indicator_running) {
        if (xSemaphoreTake(
                led_indicator_signal,
                LED_INDICATOR_POLL_INTERVAL_TICKS
            ) != pdTRUE &&
            !led_indicator_running) {
            break;
        }
        if (!led_indicator_running) {
            break;
        }

        if (xSemaphoreTake(led_indicator_mutex, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!led_indicator_ready) {
            xSemaphoreGive(led_indicator_mutex);
            break;
        }

        if (!led_indicator_enabled ||
            led_indicator_state == LED_INDICATOR_STATE_OFF) {
            if (led_indicator_write_duty_locked(0U) != ESP_OK) {
                led_indicator_record_error_locked(ESP_ERR_INVALID_STATE);
            }
            xSemaphoreGive(led_indicator_mutex);
            continue;
        }

        const led_indicator_state_t state = led_indicator_state;
        const led_indicator_pattern_entry_t *pattern =
            led_indicator_pattern_for_state(state);
        const TickType_t now_ticks = xTaskGetTickCount();
        const uint32_t now_ms =
            (uint32_t)((uint64_t)now_ticks * portTICK_PERIOD_MS);

        if (led_indicator_active_state != state ||
            led_indicator_active_pattern != pattern->pattern ||
            led_indicator_pattern_started_ms == 0U) {
            if (led_indicator_apply_pattern_locked(
                    state,
                    pattern,
                    now_ticks
                ) != ESP_OK) {
                led_indicator_record_error_locked(ESP_ERR_INVALID_STATE);
            }
            xSemaphoreGive(led_indicator_mutex);
            continue;
        }

        const uint32_t period_ms =
            led_indicator_pattern_period_ms(pattern);
        if (period_ms == 0U) {
            xSemaphoreGive(led_indicator_mutex);
            continue;
        }
        led_indicator_pattern_phase_ms =
            (now_ms - led_indicator_pattern_started_ms) % period_ms;
        if (led_indicator_write_duty_locked(
                led_indicator_phase_duty(
                    pattern,
                    led_indicator_pattern_phase_ms
                )
            ) != ESP_OK) {
            led_indicator_record_error_locked(ESP_ERR_INVALID_STATE);
        }
        xSemaphoreGive(led_indicator_mutex);
    }

    if (led_indicator_task_exit != NULL) {
        xSemaphoreGive(led_indicator_task_exit);
    }
    led_indicator_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t led_indicator_init(void) {
    if (led_indicator_ready) {
        return ESP_OK;
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_LED_INDICATOR_GPIO)) {
        led_indicator_last_error = LED_INDICATOR_ERR_GPIO_UNAVAILABLE;
        return ESP_ERR_INVALID_ARG;
    }

    if (led_indicator_mutex == NULL) {
        led_indicator_mutex = xSemaphoreCreateMutex();
    }
    if (led_indicator_signal == NULL) {
        led_indicator_signal = xSemaphoreCreateBinary();
    }
    if (led_indicator_task_exit == NULL) {
        led_indicator_task_exit = xSemaphoreCreateBinary();
    }
    if (led_indicator_mutex == NULL ||
        led_indicator_signal == NULL ||
        led_indicator_task_exit == NULL) {
        led_indicator_last_error = LED_INDICATOR_ERR_NO_MEM;
        led_indicator_release_sync_objects();
        return ESP_ERR_NO_MEM;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = LED_INDICATOR_LEDC_MODE,
        .duty_resolution = LED_INDICATOR_LEDC_DUTY_RESOLUTION,
        .timer_num = LED_INDICATOR_LEDC_TIMER,
        .freq_hz = CONFIG_LED_INDICATOR_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t result = ledc_timer_config(&timer_config);
    if (result != ESP_OK) {
        led_indicator_record_error(result);
        led_indicator_release_sync_objects();
        return result;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = CONFIG_LED_INDICATOR_GPIO,
        .speed_mode = LED_INDICATOR_LEDC_MODE,
        .channel = LED_INDICATOR_LEDC_CHANNEL,
        .timer_sel = LED_INDICATOR_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = CONFIG_LED_INDICATOR_ACTIVE_LOW ? 1U : 0U,
        },
        .deconfigure = false,
    };
    result = ledc_channel_config(&channel_config);
    if (result != ESP_OK) {
        led_indicator_record_error(result);
        led_indicator_deconfigure_ledc();
        led_indicator_release_sync_objects();
        return result;
    }

    led_indicator_enabled = true;
    led_indicator_state = LED_INDICATOR_STATE_BOOTING;
    led_indicator_last_error = LED_INDICATOR_OK;
    led_indicator_transitions = 0U;
    led_indicator_active_pattern = LED_INDICATOR_PATTERN_OFF;
    led_indicator_active_state = LED_INDICATOR_STATE_COUNT;
    led_indicator_pattern_started_ms = 0U;
    led_indicator_pattern_phase_ms = 0U;
    led_indicator_ready = true;
    led_indicator_running = true;

    if (xTaskCreate(
            led_indicator_task,
            "led_indicator",
            CONFIG_LED_INDICATOR_TASK_STACK_SIZE,
            NULL,
            CONFIG_LED_INDICATOR_TASK_PRIORITY,
            &led_indicator_task_handle
        ) != pdPASS) {
        led_indicator_running = false;
        led_indicator_ready = false;
        led_indicator_last_error = LED_INDICATOR_ERR_NO_MEM;
        led_indicator_deconfigure_ledc();
        led_indicator_release_sync_objects();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool led_indicator_is_ready(void) {
    return led_indicator_ready;
}

led_indicator_error_t led_indicator_set_state(
    led_indicator_state_t state
) {
    if (!led_indicator_ready) {
        return LED_INDICATOR_ERR_NOT_INITIALIZED;
    }
    if (!led_indicator_state_is_valid(state)) {
        return LED_INDICATOR_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(led_indicator_mutex, portMAX_DELAY) != pdTRUE) {
        return LED_INDICATOR_ERR_INVALID_STATE;
    }

    if (led_indicator_state != state) {
        led_indicator_state = state;
        led_indicator_transitions++;
        led_indicator_active_pattern = LED_INDICATOR_PATTERN_OFF;
        led_indicator_active_state = LED_INDICATOR_STATE_COUNT;
        led_indicator_pattern_started_ms = 0U;
        led_indicator_pattern_phase_ms = 0U;
        led_indicator_report_state(state);
    }
    xSemaphoreGive(led_indicator_mutex);

    if (led_indicator_signal != NULL) {
        xSemaphoreGive(led_indicator_signal);
    }
    return LED_INDICATOR_OK;
}

led_indicator_state_t led_indicator_get_state(void) {
    if (!led_indicator_ready ||
        xSemaphoreTake(led_indicator_mutex, portMAX_DELAY) != pdTRUE) {
        return LED_INDICATOR_STATE_OFF;
    }
    const led_indicator_state_t state = led_indicator_state;
    xSemaphoreGive(led_indicator_mutex);
    return state;
}

esp_err_t led_indicator_set_enabled(bool is_enabled) {
    if (!led_indicator_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(led_indicator_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (led_indicator_enabled != is_enabled) {
        led_indicator_enabled = is_enabled;
        led_indicator_active_pattern = LED_INDICATOR_PATTERN_OFF;
        led_indicator_active_state = LED_INDICATOR_STATE_COUNT;
        led_indicator_pattern_started_ms = 0U;
        led_indicator_pattern_phase_ms = 0U;
    }
    if (!is_enabled) {
        // The guardian state must take effect even if the task is between
        // pattern updates, so force the physical output off synchronously.
        const esp_err_t result = led_indicator_write_duty_locked(0U);
        if (result != ESP_OK) {
            led_indicator_record_error_locked(result);
            xSemaphoreGive(led_indicator_mutex);
            return result;
        }
    }
    xSemaphoreGive(led_indicator_mutex);

    if (led_indicator_signal != NULL) {
        xSemaphoreGive(led_indicator_signal);
    }
    return ESP_OK;
}

led_indicator_snapshot_t led_indicator_get_snapshot(void) {
    led_indicator_snapshot_t snapshot = {0};
    if (led_indicator_mutex == NULL) {
        snapshot.state = led_indicator_state;
        snapshot.is_enabled = led_indicator_enabled;
        snapshot.is_ready = led_indicator_ready;
        snapshot.transitions = led_indicator_transitions;
        snapshot.last_error = led_indicator_last_error;
        return snapshot;
    }
    if (xSemaphoreTake(led_indicator_mutex, portMAX_DELAY) != pdTRUE) {
        return snapshot;
    }

    snapshot.state = led_indicator_state;
    snapshot.is_enabled = led_indicator_enabled;
    snapshot.is_ready = led_indicator_ready;
    snapshot.transitions = led_indicator_transitions;
    snapshot.last_error = led_indicator_last_error;

    xSemaphoreGive(led_indicator_mutex);
    return snapshot;
}

const char *led_indicator_state_name(led_indicator_state_t state) {
    switch (state) {
        case LED_INDICATOR_STATE_OFF:
            return "off";
        case LED_INDICATOR_STATE_BOOTING:
            return "booting";
        case LED_INDICATOR_STATE_PROVISIONING:
            return "provisioning";
        case LED_INDICATOR_STATE_WIFI_CONNECTING:
            return "wifi_connecting";
        case LED_INDICATOR_STATE_CONNECTING:
            return "connecting";
        case LED_INDICATOR_STATE_IDLE:
            return "idle";
        case LED_INDICATOR_STATE_LISTENING:
            return "listening";
        case LED_INDICATOR_STATE_THINKING:
            return "thinking";
        case LED_INDICATOR_STATE_SPEAKING:
            return "speaking";
        case LED_INDICATOR_STATE_MUTED:
            return "muted";
        case LED_INDICATOR_STATE_ERROR:
            return "error";
        case LED_INDICATOR_STATE_LOW_BATTERY:
            return "low_battery";
        case LED_INDICATOR_STATE_FACTORY_RESET:
            return "factory_reset";
        case LED_INDICATOR_STATE_OTA_UPGRADING:
            return "ota_upgrading";
        default:
            return "off";
    }
}

const char *led_indicator_error_name(led_indicator_error_t error) {
    switch (error) {
        case LED_INDICATOR_OK:
            return "ok";
        case LED_INDICATOR_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case LED_INDICATOR_ERR_INVALID_STATE:
            return "invalid_state";
        case LED_INDICATOR_ERR_GPIO_UNAVAILABLE:
            return "gpio_unavailable";
        case LED_INDICATOR_ERR_NO_MEM:
            return "no_memory";
        default:
            return "gpio_unavailable";
    }
}

static void led_indicator_shutdown(void) {
    const bool has_resources = led_indicator_ready ||
        led_indicator_task_handle != NULL ||
        led_indicator_mutex != NULL ||
        led_indicator_signal != NULL ||
        led_indicator_task_exit != NULL;
    if (!has_resources) {
        return;
    }

    led_indicator_ready = false;
    led_indicator_running = false;
    if (led_indicator_mutex != NULL) {
        (void)xSemaphoreTake(led_indicator_mutex, portMAX_DELAY);
    }
    if (led_indicator_signal != NULL) {
        xSemaphoreGive(led_indicator_signal);
    }
    if (led_indicator_mutex != NULL) {
        xSemaphoreGive(led_indicator_mutex);
    }

    if (led_indicator_task_handle != NULL) {
        if (xSemaphoreTake(
                led_indicator_task_exit,
                pdMS_TO_TICKS(2000)
            ) != pdTRUE) {
            // Keep the task and its synchronization objects alive rather
            // than freeing state that a blocked task may still use.
            ESP_LOGE(TAG, "indicator task did not stop; keeping resources alive");
            return;
        }
        led_indicator_task_handle = NULL;
    }

    led_indicator_deconfigure_ledc();
    led_indicator_release_sync_objects();
}

const module_descriptor_t *led_indicator_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "led_indicator",
        .version = "1.0.0",
        .initialize = led_indicator_init,
        .shutdown = led_indicator_shutdown,
    };
    return &descriptor;
}
