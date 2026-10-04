#include "button_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *const TAG = "button_input";

#define BUTTON_INPUT_DEBOUNCE_MS CONFIG_BUTTON_INPUT_DEBOUNCE_MS
#define BUTTON_INPUT_SHORT_PRESS_MS CONFIG_BUTTON_INPUT_SHORT_PRESS_MS
#define BUTTON_INPUT_LONG_PRESS_MS CONFIG_BUTTON_INPUT_LONG_PRESS_MS
#define BUTTON_INPUT_VERY_LONG_PRESS_MS CONFIG_BUTTON_INPUT_VERY_LONG_PRESS_MS
#define BUTTON_INPUT_DOUBLE_PRESS_MS CONFIG_BUTTON_INPUT_DOUBLE_PRESS_MS
#define BUTTON_INPUT_TASK_STACK_SIZE CONFIG_BUTTON_INPUT_TASK_STACK_SIZE
#define BUTTON_INPUT_TASK_PRIORITY CONFIG_BUTTON_INPUT_TASK_PRIORITY

typedef enum {
    BUTTON_INPUT_STATE_IDLE = 0,
    BUTTON_INPUT_STATE_DEBOUNCE_PRESS,
    BUTTON_INPUT_STATE_PRESSED,
    BUTTON_INPUT_STATE_DEBOUNCE_RELEASE,
    BUTTON_INPUT_STATE_WAIT_DOUBLE,
} button_input_state_t;

typedef struct {
    gpio_num_t gpio;
    bool active_low;
    bool configured;
    bool handler_installed;
    bool interrupt_enabled;
    bool stable_pressed;
    bool long_reported;
    bool very_long_reported;
    bool short_pending;
    button_input_state_t state;
    TickType_t state_started;
    TickType_t press_started;
    TickType_t short_released;
    uint32_t pending_short_held_ms;
    uint32_t short_press_count;
    uint32_t long_press_count;
    uint32_t double_press_count;
    uint32_t very_long_press_count;
} button_input_button_t;

static button_input_button_t button_input_buttons[BUTTON_INPUT_MAX_BUTTONS];
static size_t button_input_button_count;
static button_input_event_handler_t button_input_handler;
static SemaphoreHandle_t button_input_lock;
static SemaphoreHandle_t button_input_task_exit;
static TaskHandle_t button_input_task_handle;
static bool button_input_ready;
static bool button_input_running;
static bool button_input_isr_service_installed_by_us;

static bool button_input_gpio_is_pressed(
    const button_input_button_t *button
) {
    const int level = gpio_get_level(button->gpio);
    return button->active_low ? level == 0 : level != 0;
}

static uint32_t button_input_ticks_to_ms(TickType_t ticks) {
    return (uint32_t)((uint64_t)ticks * 1000U / configTICK_RATE_HZ);
}

static TickType_t button_input_ms_to_ticks(uint32_t milliseconds) {
    const TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    return ticks == 0 ? 1 : ticks;
}

static bool button_input_time_reached(
    TickType_t now,
    TickType_t started,
    TickType_t interval
) {
    return (TickType_t)(now - started) >= interval;
}

static TickType_t button_input_time_remaining(
    TickType_t now,
    TickType_t started,
    TickType_t interval
) {
    const TickType_t elapsed = now - started;
    return elapsed >= interval ? 0 : interval - elapsed;
}

static void button_input_publish_pressed(
    button_input_button_t *button,
    bool is_pressed
) {
    if (xSemaphoreTake(button_input_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    button->stable_pressed = is_pressed;
    xSemaphoreGive(button_input_lock);
}

static void button_input_emit_event(
    uint8_t button_index,
    button_input_gesture_t gesture,
    uint32_t held_ms
) {
    button_input_event_handler_t handler = NULL;

    if (xSemaphoreTake(button_input_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    switch (gesture) {
        case BUTTON_INPUT_GESTURE_SHORT_PRESS:
            button_input_buttons[button_index].short_press_count++;
            break;
        case BUTTON_INPUT_GESTURE_LONG_PRESS:
            button_input_buttons[button_index].long_press_count++;
            break;
        case BUTTON_INPUT_GESTURE_DOUBLE_PRESS:
            button_input_buttons[button_index].double_press_count++;
            break;
        case BUTTON_INPUT_GESTURE_VERY_LONG_PRESS:
            button_input_buttons[button_index].very_long_press_count++;
            break;
        case BUTTON_INPUT_GESTURE_NONE:
        default:
            break;
    }
    handler = button_input_handler;
    xSemaphoreGive(button_input_lock);

    if (handler != NULL) {
        const button_input_event_t event = {
            .button_index = button_index,
            .gesture = gesture,
            .held_ms = held_ms,
            .timestamp_ms = button_input_ticks_to_ms(xTaskGetTickCount()),
        };
        // The handler runs without the lock so it can read snapshots or
        // replace itself without deadlocking the state machine.
        handler(&event);
    }
}

static void button_input_emit_pending_short(
    uint8_t button_index,
    button_input_button_t *button
) {
    if (!button->short_pending) {
        return;
    }
    button_input_emit_event(
        button_index,
        BUTTON_INPUT_GESTURE_SHORT_PRESS,
        button->pending_short_held_ms
    );
    button->short_pending = false;
    button->pending_short_held_ms = 0;
}

static void button_input_enter_debounce_press(
    button_input_button_t *button,
    TickType_t now
) {
    button->state = BUTTON_INPUT_STATE_DEBOUNCE_PRESS;
    button->state_started = now;
    button_input_publish_pressed(button, false);
}

static void button_input_enter_pressed(
    button_input_button_t *button,
    TickType_t now
) {
    button->state = BUTTON_INPUT_STATE_PRESSED;
    button->state_started = now;
    button->press_started = now;
    button->long_reported = false;
    button->very_long_reported = false;
    button_input_publish_pressed(button, true);
}

static void button_input_handle_release(
    uint8_t button_index,
    button_input_button_t *button,
    TickType_t now
) {
    const uint32_t held_ms = button_input_ticks_to_ms(
        now - button->press_started
    );
    const TickType_t long_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_LONG_PRESS_MS);
    const TickType_t very_long_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_VERY_LONG_PRESS_MS);

    button_input_publish_pressed(button, false);

    // A delayed task can learn about both thresholds at once. Report the
    // strongest gesture once instead of emitting two actions for one hold.
    const bool very_long_reached = !button->very_long_reported &&
        button_input_time_reached(
            now,
            button->press_started,
            very_long_ticks
        );
    const bool long_reached = !button->long_reported &&
        button_input_time_reached(
            now,
            button->press_started,
            long_ticks
        );
    if (very_long_reached) {
        button_input_emit_event(
            button_index,
            BUTTON_INPUT_GESTURE_VERY_LONG_PRESS,
            held_ms
        );
        button->very_long_reported = true;
        button->long_reported = true;
    } else if (long_reached) {
        button_input_emit_pending_short(button_index, button);
        button_input_emit_event(
            button_index,
            BUTTON_INPUT_GESTURE_LONG_PRESS,
            held_ms
        );
        button->long_reported = true;
    }

    if (button->long_reported) {
        button->short_pending = false;
        button->pending_short_held_ms = 0;
        button->state = BUTTON_INPUT_STATE_IDLE;
        return;
    }

    if (button->short_pending) {
        button_input_emit_event(
            button_index,
            BUTTON_INPUT_GESTURE_DOUBLE_PRESS,
            held_ms
        );
        button->short_pending = false;
        button->pending_short_held_ms = 0;
        button->state = BUTTON_INPUT_STATE_IDLE;
        return;
    }

    if (held_ms >= BUTTON_INPUT_SHORT_PRESS_MS) {
        button->short_pending = true;
        button->pending_short_held_ms = held_ms;
        button->short_released = now;
        button->state = BUTTON_INPUT_STATE_WAIT_DOUBLE;
        return;
    }

    button->state = BUTTON_INPUT_STATE_IDLE;
}

// One debounced state machine runs per button. GPIO edges only wake this
// task, so bounce is filtered by stable elapsed time rather than ISR timing.
static void button_input_update_button(
    uint8_t button_index,
    button_input_button_t *button,
    TickType_t now
) {
    const bool raw_pressed = button_input_gpio_is_pressed(button);
    const TickType_t debounce_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_DEBOUNCE_MS);

    switch (button->state) {
        case BUTTON_INPUT_STATE_IDLE:
            if (raw_pressed) {
                button_input_enter_debounce_press(button, now);
            }
            break;

        case BUTTON_INPUT_STATE_DEBOUNCE_PRESS:
            if (!raw_pressed) {
                button->state = button->short_pending
                    ? BUTTON_INPUT_STATE_WAIT_DOUBLE
                    : BUTTON_INPUT_STATE_IDLE;
                break;
            }
            if (button_input_time_reached(
                    now,
                    button->state_started,
                    debounce_ticks
                )) {
                button_input_enter_pressed(button, now);
            }
            break;

        case BUTTON_INPUT_STATE_PRESSED:
            if (!raw_pressed) {
                button->state = BUTTON_INPUT_STATE_DEBOUNCE_RELEASE;
                button->state_started = now;
                break;
            }
            // A delayed sample can cross both thresholds at once. Report the
            // strongest gesture so one hold never triggers two actions.
            if (!button->very_long_reported &&
                button_input_time_reached(
                    now,
                    button->press_started,
                    button_input_ms_to_ticks(
                        BUTTON_INPUT_VERY_LONG_PRESS_MS
                    )
                )) {
                button_input_emit_event(
                    button_index,
                    BUTTON_INPUT_GESTURE_VERY_LONG_PRESS,
                    button_input_ticks_to_ms(now - button->press_started)
                );
                button->very_long_reported = true;
                button->long_reported = true;
            } else if (!button->long_reported &&
                button_input_time_reached(
                    now,
                    button->press_started,
                    button_input_ms_to_ticks(BUTTON_INPUT_LONG_PRESS_MS)
                )) {
                button_input_emit_pending_short(button_index, button);
                button_input_emit_event(
                    button_index,
                    BUTTON_INPUT_GESTURE_LONG_PRESS,
                    button_input_ticks_to_ms(now - button->press_started)
                );
                button->long_reported = true;
            }
            break;

        case BUTTON_INPUT_STATE_DEBOUNCE_RELEASE:
            if (raw_pressed) {
                // The release was bounce; resume the same physical press.
                button->state = BUTTON_INPUT_STATE_PRESSED;
                break;
            }
            if (button_input_time_reached(
                    now,
                    button->state_started,
                    debounce_ticks
                )) {
                button_input_handle_release(button_index, button, now);
            }
            break;

        case BUTTON_INPUT_STATE_WAIT_DOUBLE:
            if (raw_pressed) {
                button_input_enter_debounce_press(button, now);
                break;
            }
            if (button_input_time_reached(
                    now,
                    button->short_released,
                    button_input_ms_to_ticks(
                        BUTTON_INPUT_DOUBLE_PRESS_MS
                    )
                )) {
                button_input_emit_pending_short(button_index, button);
                button->state = BUTTON_INPUT_STATE_IDLE;
            }
            break;

        default:
            button->state = BUTTON_INPUT_STATE_IDLE;
            break;
    }
}

static TickType_t button_input_next_delay(TickType_t now) {
    TickType_t next_delay = portMAX_DELAY;
    const TickType_t debounce_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_DEBOUNCE_MS);
    const TickType_t long_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_LONG_PRESS_MS);
    const TickType_t very_long_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_VERY_LONG_PRESS_MS);
    const TickType_t double_ticks =
        button_input_ms_to_ticks(BUTTON_INPUT_DOUBLE_PRESS_MS);

    for (size_t index = 0; index < button_input_button_count; ++index) {
        const button_input_button_t *button = &button_input_buttons[index];
        TickType_t remaining = portMAX_DELAY;

        switch (button->state) {
            case BUTTON_INPUT_STATE_DEBOUNCE_PRESS:
            case BUTTON_INPUT_STATE_DEBOUNCE_RELEASE:
                remaining = button_input_time_remaining(
                    now,
                    button->state_started,
                    debounce_ticks
                );
                break;
            case BUTTON_INPUT_STATE_PRESSED:
                if (!button->long_reported) {
                    remaining = button_input_time_remaining(
                        now,
                        button->press_started,
                        long_ticks
                    );
                }
                if (!button->very_long_reported) {
                    const TickType_t very_long_remaining =
                        button_input_time_remaining(
                            now,
                            button->press_started,
                            very_long_ticks
                        );
                    if (very_long_remaining < remaining) {
                        remaining = very_long_remaining;
                    }
                }
                break;
            case BUTTON_INPUT_STATE_WAIT_DOUBLE:
                remaining = button_input_time_remaining(
                    now,
                    button->short_released,
                    double_ticks
                );
                break;
            case BUTTON_INPUT_STATE_IDLE:
            default:
                continue;
        }

        if (remaining < next_delay) {
            next_delay = remaining;
        }
    }
    return next_delay;
}

static void button_input_task(void *argument) {
    (void)argument;

    while (button_input_running) {
        const TickType_t now = xTaskGetTickCount();
        for (size_t index = 0; index < button_input_button_count; ++index) {
            button_input_update_button(
                (uint8_t)index,
                &button_input_buttons[index],
                now
            );
        }

        const TickType_t delay = button_input_next_delay(
            xTaskGetTickCount()
        );
        if (delay == 0) {
            taskYIELD();
            continue;
        }
        (void)ulTaskNotifyTake(pdTRUE, delay);
    }

    button_input_task_handle = NULL;
    if (button_input_task_exit != NULL) {
        xSemaphoreGive(button_input_task_exit);
    }
    vTaskDelete(NULL);
}

static void IRAM_ATTR button_input_gpio_isr(void *argument) {
    (void)argument;
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (button_input_task_handle != NULL) {
        vTaskNotifyGiveFromISR(
            button_input_task_handle,
            &higher_priority_task_woken
        );
    }
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t button_input_configure_gpio(
    gpio_num_t gpio,
    bool active_low
) {
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low
            ? GPIO_PULLDOWN_DISABLE
            : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static esp_err_t button_input_install_isr_service(void) {
    const esp_err_t result = gpio_install_isr_service(0);
    if (result == ESP_OK) {
        button_input_isr_service_installed_by_us = true;
        return ESP_OK;
    }
    if (result == ESP_ERR_INVALID_STATE) {
        // Another module owns the shared service. Handlers can still be added
        // and removed safely, so the module does not treat this as an error.
        button_input_isr_service_installed_by_us = false;
        return ESP_OK;
    }
    return result;
}

static void button_input_remove_handlers(void) {
    for (size_t index = 0; index < button_input_button_count; ++index) {
        button_input_button_t *button = &button_input_buttons[index];
        if (button->interrupt_enabled) {
            (void)gpio_intr_disable(button->gpio);
            (void)gpio_set_intr_type(button->gpio, GPIO_INTR_DISABLE);
            button->interrupt_enabled = false;
        }
        if (button->handler_installed) {
            (void)gpio_isr_handler_remove(button->gpio);
            button->handler_installed = false;
        }
    }
}

static void button_input_restore_gpios(void) {
    for (size_t index = 0; index < button_input_button_count; ++index) {
        button_input_button_t *button = &button_input_buttons[index];
        if (!button->configured) {
            continue;
        }
        (void)gpio_reset_pin(button->gpio);
        (void)gpio_set_direction(button->gpio, GPIO_MODE_INPUT);
        button->configured = false;
    }
}

static void button_input_release_resources(void) {
    button_input_ready = false;
    // Stop and remove every ISR before the task can be deleted. Otherwise a
    // late edge can notify a handle that no longer belongs to a live task.
    button_input_remove_handlers();
    button_input_running = false;

    if (button_input_task_handle != NULL) {
        xTaskNotifyGive(button_input_task_handle);
        const bool task_stopped = button_input_task_exit != NULL &&
            xSemaphoreTake(
                button_input_task_exit,
                pdMS_TO_TICKS(2000)
            ) == pdTRUE;
        if (!task_stopped) {
            // The task can be inside a user handler. Keep every resource
            // alive rather than deleting memory the task may still touch.
            ESP_LOGE(TAG, "button task did not stop; keeping resources alive");
            return;
        }
    }

    // The task is stopped, so the lock and its counters can no longer be used.
    button_input_restore_gpios();
    if (button_input_isr_service_installed_by_us) {
        (void)gpio_uninstall_isr_service();
        button_input_isr_service_installed_by_us = false;
    }
    if (button_input_task_exit != NULL) {
        vSemaphoreDelete(button_input_task_exit);
        button_input_task_exit = NULL;
    }
    if (button_input_lock != NULL) {
        vSemaphoreDelete(button_input_lock);
        button_input_lock = NULL;
    }
    button_input_handler = NULL;
    button_input_task_handle = NULL;
}

static esp_err_t button_input_add_handler(
    button_input_button_t *button
) {
    esp_err_t result = gpio_isr_handler_add(
        button->gpio,
        button_input_gpio_isr,
        NULL
    );
    if (result != ESP_OK) {
        return result;
    }
    button->handler_installed = true;

    result = gpio_set_intr_type(button->gpio, GPIO_INTR_ANYEDGE);
    if (result == ESP_OK) {
        result = gpio_intr_enable(button->gpio);
    }
    if (result == ESP_OK) {
        button->interrupt_enabled = true;
        return ESP_OK;
    }

    (void)gpio_isr_handler_remove(button->gpio);
    button->handler_installed = false;
    return result;
}

esp_err_t button_input_init(void) {
    if (button_input_ready) {
        return ESP_OK;
    }

    const gpio_num_t primary_gpio =
        (gpio_num_t)CONFIG_BUTTON_INPUT_PRIMARY_GPIO;
    const bool primary_active_low =
        CONFIG_BUTTON_INPUT_PRIMARY_ACTIVE_LOW;
#if CONFIG_BUTTON_INPUT_SECONDARY_ENABLED
    const gpio_num_t secondary_gpio =
        (gpio_num_t)CONFIG_BUTTON_INPUT_SECONDARY_GPIO;
    const bool secondary_active_low =
        CONFIG_BUTTON_INPUT_SECONDARY_ACTIVE_LOW;
#endif

    if (primary_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_BUTTON_INPUT_SECONDARY_ENABLED
    if (secondary_gpio == GPIO_NUM_NC || secondary_gpio == primary_gpio) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    if (button_input_lock == NULL) {
        button_input_lock = xSemaphoreCreateMutex();
        if (button_input_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (button_input_task_exit == NULL) {
        button_input_task_exit = xSemaphoreCreateBinary();
        if (button_input_task_exit == NULL) {
            if (button_input_lock != NULL) {
                vSemaphoreDelete(button_input_lock);
                button_input_lock = NULL;
            }
            return ESP_ERR_NO_MEM;
        }
    }

    memset(button_input_buttons, 0, sizeof(button_input_buttons));
    button_input_button_count = 1;
    button_input_buttons[0].gpio = primary_gpio;
    button_input_buttons[0].active_low = primary_active_low;
#if CONFIG_BUTTON_INPUT_SECONDARY_ENABLED
    button_input_buttons[1].gpio = secondary_gpio;
    button_input_buttons[1].active_low = secondary_active_low;
    button_input_button_count = 2;
#endif

    esp_err_t result = button_input_install_isr_service();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s",
                 esp_err_to_name(result));
        button_input_release_resources();
        return result;
    }

    for (size_t index = 0; index < button_input_button_count; ++index) {
        button_input_button_t *button = &button_input_buttons[index];
        result = button_input_configure_gpio(
            button->gpio,
            button->active_low
        );
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "GPIO %d configure failed: %s",
                     (int)button->gpio,
                     esp_err_to_name(result));
            button_input_release_resources();
            return result;
        }
        button->configured = true;

        const bool raw_pressed = button_input_gpio_is_pressed(button);
        button->stable_pressed = raw_pressed;
        if (raw_pressed) {
            // A key already held at initialization still receives debounce
            // and hold timing instead of being silently ignored.
            button_input_enter_debounce_press(button, xTaskGetTickCount());
        }
    }

    button_input_running = true;
    if (xTaskCreate(
            button_input_task,
            "button_input",
            BUTTON_INPUT_TASK_STACK_SIZE,
            NULL,
            BUTTON_INPUT_TASK_PRIORITY,
            &button_input_task_handle
        ) != pdPASS) {
        button_input_running = false;
        button_input_release_resources();
        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0; index < button_input_button_count; ++index) {
        result = button_input_add_handler(&button_input_buttons[index]);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "GPIO %d handler install failed: %s",
                     (int)button_input_buttons[index].gpio,
                     esp_err_to_name(result));
            button_input_release_resources();
            return result;
        }
    }

    button_input_ready = true;
    ESP_LOGI(TAG, "button input ready: %u button(s)",
             (unsigned)button_input_button_count);
    return ESP_OK;
}

bool button_input_is_ready(void) {
    return button_input_ready;
}

esp_err_t button_input_set_event_handler(
    button_input_event_handler_t handler
) {
    if (!button_input_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(button_input_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    button_input_handler = handler;
    xSemaphoreGive(button_input_lock);
    return ESP_OK;
}

button_input_snapshot_t button_input_get_snapshot(void) {
    button_input_snapshot_t snapshot = {0};
    snapshot.button_count = (uint8_t)button_input_button_count;
    if (button_input_lock == NULL ||
        xSemaphoreTake(button_input_lock, portMAX_DELAY) != pdTRUE) {
        return snapshot;
    }

    for (size_t index = 0; index < button_input_button_count; ++index) {
        snapshot.is_pressed[index] =
            button_input_buttons[index].stable_pressed;
        snapshot.short_press_count[index] =
            button_input_buttons[index].short_press_count;
        snapshot.long_press_count[index] =
            button_input_buttons[index].long_press_count;
        snapshot.double_press_count[index] =
            button_input_buttons[index].double_press_count;
        snapshot.very_long_press_count[index] =
            button_input_buttons[index].very_long_press_count;
    }

    xSemaphoreGive(button_input_lock);
    return snapshot;
}

const char *button_input_gesture_name(button_input_gesture_t gesture) {
    switch (gesture) {
        case BUTTON_INPUT_GESTURE_SHORT_PRESS:
            return "short_press";
        case BUTTON_INPUT_GESTURE_LONG_PRESS:
            return "long_press";
        case BUTTON_INPUT_GESTURE_VERY_LONG_PRESS:
            return "very_long_press";
        case BUTTON_INPUT_GESTURE_DOUBLE_PRESS:
            return "double_press";
        case BUTTON_INPUT_GESTURE_NONE:
        default:
            return "none";
    }
}

const char *button_input_error_name(button_input_error_t error) {
    switch (error) {
        case BUTTON_INPUT_OK:
            return "ok";
        case BUTTON_INPUT_ERR_NOT_INITIALIZED:
            return "button_input_not_initialized";
        case BUTTON_INPUT_ERR_INVALID_ARGUMENT:
            return "button_input_invalid_argument";
        case BUTTON_INPUT_ERR_GPIO:
            return "button_input_gpio_error";
        case BUTTON_INPUT_ERR_NO_MEM:
            return "button_input_no_memory";
        default:
            return "button_input_internal";
    }
}

static void button_input_shutdown(void) {
    button_input_release_resources();
}

const module_descriptor_t *button_input_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "button_input",
        .version = "1.0.0",
        .initialize = button_input_init,
        .shutdown = button_input_shutdown,
    };
    return &descriptor;
}
