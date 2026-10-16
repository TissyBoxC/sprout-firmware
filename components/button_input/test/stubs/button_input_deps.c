#include "button_input_host.h"

#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

typedef struct {
    int tokens;
} host_semaphore_t;

static host_semaphore_t host_mutex;
static host_semaphore_t host_task_exit;
static TaskFunction_t host_task_function;
static void *host_task_argument;
static TaskHandle_t host_task_handle;
static TickType_t host_tick_count;
static int host_gpio_levels[64];
static gpio_config_t host_gpio_configs[8];
static size_t host_gpio_config_count;
static int host_handler_add_count;
static int host_handler_remove_count;
static int host_intr_disable_count;
static int host_gpio_reset_count;
static int host_task_create_count;
static int host_semaphore_delete_count;
static int host_isr_install_count;
static int host_isr_uninstall_count;
static bool host_isr_service_installed;

static int host_create_mutex_result = 1;
static int host_create_binary_result = 1;
static int host_task_create_result = 1;
static esp_err_t host_isr_service_result = ESP_OK;
static int host_fail_gpio_config_at = -1;
static int host_fail_handler_add_at = -1;
static uint32_t host_notify_tokens;

static bool host_gpio_in_range(gpio_num_t gpio) {
    return (int)gpio >= 0 && (int)gpio < (int)(sizeof(host_gpio_levels) /
                                                sizeof(host_gpio_levels[0]));
}

void button_input_host_reset(void) {
    memset(host_gpio_levels, 0, sizeof(host_gpio_levels));
    for (size_t index = 0; index < sizeof(host_gpio_levels) /
                                    sizeof(host_gpio_levels[0]); ++index) {
        host_gpio_levels[index] = 1;
    }
    memset(host_gpio_configs, 0, sizeof(host_gpio_configs));
    host_gpio_config_count = 0;
    host_handler_add_count = 0;
    host_handler_remove_count = 0;
    host_intr_disable_count = 0;
    host_gpio_reset_count = 0;
    host_task_create_count = 0;
    host_semaphore_delete_count = 0;
    host_isr_install_count = 0;
    host_isr_uninstall_count = 0;
    host_isr_service_installed = false;
    host_create_mutex_result = 1;
    host_create_binary_result = 1;
    host_task_create_result = 1;
    host_isr_service_result = ESP_OK;
    host_fail_gpio_config_at = -1;
    host_fail_handler_add_at = -1;
    host_notify_tokens = 0;
    host_mutex.tokens = 1;
    host_task_exit.tokens = 0;
    host_task_function = NULL;
    host_task_argument = NULL;
    host_task_handle = NULL;
    host_tick_count = 1000;
}

void button_input_host_set_tick_count(TickType_t ticks) {
    host_tick_count = ticks;
}

void button_input_host_advance_ticks(TickType_t delta) {
    host_tick_count += delta;
}

void button_input_host_set_gpio_level(gpio_num_t gpio, int level) {
    if (host_gpio_in_range(gpio)) {
        host_gpio_levels[(int)gpio] = level == 0 ? 0 : 1;
    }
}

void button_input_host_set_create_mutex_result(int ok) {
    host_create_mutex_result = ok;
}

void button_input_host_set_create_binary_result(int ok) {
    host_create_binary_result = ok;
}

void button_input_host_set_task_create_result(int ok) {
    host_task_create_result = ok;
}

void button_input_host_set_isr_service_result(esp_err_t result) {
    host_isr_service_result = result;
}

void button_input_host_fail_gpio_config_at(int call_number) {
    host_fail_gpio_config_at = call_number;
}

void button_input_host_fail_handler_add_at(int call_number) {
    host_fail_handler_add_at = call_number;
}

int button_input_host_gpio_config_calls(void) {
    return (int)host_gpio_config_count;
}

gpio_config_t button_input_host_gpio_config_at(size_t index) {
    if (index >= host_gpio_config_count) {
        return (gpio_config_t){0};
    }
    return host_gpio_configs[index];
}

int button_input_host_handler_add_calls(void) {
    return host_handler_add_count;
}

int button_input_host_handler_remove_calls(void) {
    return host_handler_remove_count;
}

int button_input_host_intr_disable_calls(void) {
    return host_intr_disable_count;
}

int button_input_host_gpio_reset_calls(void) {
    return host_gpio_reset_count;
}

int button_input_host_task_create_calls(void) {
    return host_task_create_count;
}

int button_input_host_semaphore_delete_calls(void) {
    return host_semaphore_delete_count;
}

int button_input_host_isr_install_calls(void) {
    return host_isr_install_count;
}

int button_input_host_isr_uninstall_calls(void) {
    return host_isr_uninstall_count;
}

bool button_input_host_isr_service_installed(void) {
    return host_isr_service_installed;
}

bool button_input_host_task_registered(void) {
    return host_task_function != NULL;
}

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

int gpio_get_level(gpio_num_t gpio) {
    if (!host_gpio_in_range(gpio)) {
        return 1;
    }
    return host_gpio_levels[(int)gpio];
}

esp_err_t gpio_config(const gpio_config_t *config) {
    ++host_gpio_config_count;
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_gpio_config_count <=
        (size_t)(sizeof(host_gpio_configs) / sizeof(host_gpio_configs[0]))) {
        host_gpio_configs[host_gpio_config_count - 1] = *config;
    }
    if (host_fail_gpio_config_at > 0 &&
        host_gpio_config_count == (size_t)host_fail_gpio_config_at) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t gpio_install_isr_service(int intr_alloc_flags) {
    (void)intr_alloc_flags;
    ++host_isr_install_count;
    if (host_isr_service_result != ESP_OK) {
        return host_isr_service_result;
    }
    host_isr_service_installed = true;
    return ESP_OK;
}

void gpio_uninstall_isr_service(void) {
    ++host_isr_uninstall_count;
    host_isr_service_installed = false;
}

esp_err_t gpio_isr_handler_add(
    gpio_num_t gpio,
    gpio_isr_t isr_handler,
    void *argument
) {
    (void)gpio;
    (void)isr_handler;
    (void)argument;
    ++host_handler_add_count;
    if (host_fail_handler_add_at > 0 &&
        host_handler_add_count == host_fail_handler_add_at) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t gpio_isr_handler_remove(gpio_num_t gpio) {
    (void)gpio;
    ++host_handler_remove_count;
    return ESP_OK;
}

esp_err_t gpio_set_intr_type(gpio_num_t gpio, gpio_intr_type_t intr_type) {
    (void)gpio;
    (void)intr_type;
    return ESP_OK;
}

esp_err_t gpio_intr_enable(gpio_num_t gpio) {
    (void)gpio;
    return ESP_OK;
}

esp_err_t gpio_intr_disable(gpio_num_t gpio) {
    (void)gpio;
    ++host_intr_disable_count;
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio) {
    (void)gpio;
    ++host_gpio_reset_count;
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio, gpio_mode_t mode) {
    (void)gpio;
    (void)mode;
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (!host_create_mutex_result) {
        return NULL;
    }
    return &host_mutex;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (!host_create_binary_result) {
        return NULL;
    }
    return &host_task_exit;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)timeout;
    host_semaphore_t *state = (host_semaphore_t *)semaphore;
    if (state == NULL || state->tokens <= 0) {
        return pdFALSE;
    }
    --state->tokens;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    host_semaphore_t *state = (host_semaphore_t *)semaphore;
    if (state == NULL) {
        return pdFALSE;
    }
    ++state->tokens;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    ++host_semaphore_delete_count;
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    UBaseType_t priority,
    TaskHandle_t *handle
) {
    (void)name;
    (void)stack_size;
    (void)priority;
    ++host_task_create_count;
    if (!host_task_create_result) {
        if (handle != NULL) {
            *handle = NULL;
        }
        return pdFAIL;
    }
    host_task_function = task;
    host_task_argument = argument;
    host_task_handle = (TaskHandle_t)&host_task_function;
    if (handle != NULL) {
        *handle = host_task_handle;
    }
    // The host does not schedule the task. Make the exit wait cancelable so
    // shutdown can model the task observing the stop flag and exiting.
    host_task_exit.tokens = 1;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

void vTaskDelay(TickType_t ticks) {
    host_tick_count += ticks;
}

void vTaskNotifyGive(TaskHandle_t task) {
    (void)task;
    ++host_notify_tokens;
}

void vTaskNotifyGiveFromISR(
    TaskHandle_t task,
    BaseType_t *higher_priority_task_woken
) {
    (void)task;
    ++host_notify_tokens;
    if (higher_priority_task_woken != NULL) {
        *higher_priority_task_woken = pdFALSE;
    }
}

void xTaskNotifyGive(TaskHandle_t task) {
    (void)task;
    ++host_notify_tokens;
}

uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait) {
    (void)ticks_to_wait;
    if (host_notify_tokens == 0) {
        return 0;
    }
    if (clear_on_exit) {
        host_notify_tokens = 0;
    } else {
        --host_notify_tokens;
    }
    return 1;
}

TickType_t xTaskGetTickCount(void) {
    return host_tick_count;
}

void taskYIELD(void) {
}
