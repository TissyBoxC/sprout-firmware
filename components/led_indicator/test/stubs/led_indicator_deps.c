#include "driver/ledc.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

#include <string.h>

// Host stand-ins for the LEDC peripheral and the FreeRTOS primitives. The
// pattern task is recorded but never scheduled, so init and the public state
// API stay deterministic. Duty writes are captured for assertions.
static bool host_gpio_valid = true;
static bool host_create_mutex_fails;
static bool host_create_binary_fails;
static bool host_create_task_fails;
static esp_err_t host_timer_config_result = ESP_OK;
static esp_err_t host_channel_config_result = ESP_OK;
static esp_err_t host_set_duty_result = ESP_OK;
static esp_err_t host_update_duty_result = ESP_OK;
static TickType_t host_tick_count = 1000;

static int host_mutex;
static int host_signal;
static int host_task_exit;
static bool host_binary_handed_out_signal;

static int host_timer_config_calls;
static int host_channel_config_calls;
static int host_set_duty_calls;
static int host_update_duty_calls;
static int host_stop_calls;
static int host_timer_pause_calls;
static int host_semaphore_delete_calls;
static int host_task_create_calls;

static uint32_t host_last_duty = 0xffffffffu;
static ledc_timer_config_t host_last_timer_config;
static ledc_channel_config_t host_last_channel_config;
static TaskFunction_t host_task_function;

bool led_indicator_host_gpio_is_valid(int gpio) {
    (void)gpio;
    return host_gpio_valid;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return host_create_mutex_fails ? NULL : &host_mutex;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (host_create_binary_fails) {
        return NULL;
    }
    // The module creates a signal and an exit semaphore; hand out two distinct
    // addresses so neither aliases the other.
    if (!host_binary_handed_out_signal) {
        host_binary_handed_out_signal = true;
        return &host_signal;
    }
    return &host_task_exit;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)semaphore;
    (void)timeout;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    ++host_semaphore_delete_calls;
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
    (void)argument;
    (void)priority;
    ++host_task_create_calls;
    if (host_create_task_fails) {
        return pdFAIL;
    }
    host_task_function = task;
    // No task is actually scheduled, so the module must not receive a handle
    // it could wait on during shutdown.
    if (handle != NULL) {
        *handle = NULL;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

TickType_t xTaskGetTickCount(void) {
    return host_tick_count;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *config) {
    ++host_timer_config_calls;
    if (config != NULL) {
        host_last_timer_config = *config;
    }
    return host_timer_config_result;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *config) {
    ++host_channel_config_calls;
    if (config != NULL) {
        host_last_channel_config = *config;
    }
    return host_channel_config_result;
}

esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty) {
    (void)speed_mode;
    (void)channel;
    ++host_set_duty_calls;
    if (host_set_duty_result != ESP_OK) {
        return host_set_duty_result;
    }
    host_last_duty = duty;
    return ESP_OK;
}

esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel) {
    (void)speed_mode;
    (void)channel;
    ++host_update_duty_calls;
    return host_update_duty_result;
}

esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level) {
    (void)speed_mode;
    (void)channel;
    (void)idle_level;
    ++host_stop_calls;
    return ESP_OK;
}

esp_err_t ledc_timer_pause(ledc_mode_t speed_mode, ledc_timer_t timer_num) {
    (void)speed_mode;
    (void)timer_num;
    ++host_timer_pause_calls;
    return ESP_OK;
}

// Test hooks.
void led_indicator_host_reset(void) {
    host_gpio_valid = true;
    host_create_mutex_fails = false;
    host_create_binary_fails = false;
    host_create_task_fails = false;
    host_timer_config_result = ESP_OK;
    host_channel_config_result = ESP_OK;
    host_set_duty_result = ESP_OK;
    host_update_duty_result = ESP_OK;
    host_tick_count = 1000;
    host_binary_handed_out_signal = false;
    host_timer_config_calls = 0;
    host_channel_config_calls = 0;
    host_set_duty_calls = 0;
    host_update_duty_calls = 0;
    host_stop_calls = 0;
    host_timer_pause_calls = 0;
    host_semaphore_delete_calls = 0;
    host_task_create_calls = 0;
    host_last_duty = 0xffffffffu;
    host_task_function = NULL;
    memset(&host_last_timer_config, 0, sizeof(host_last_timer_config));
    memset(&host_last_channel_config, 0, sizeof(host_last_channel_config));
}

void led_indicator_host_set_gpio_valid(bool valid) {
    host_gpio_valid = valid;
}

void led_indicator_host_set_create_mutex_fails(bool fails) {
    host_create_mutex_fails = fails;
}

void led_indicator_host_set_create_binary_fails(bool fails) {
    host_create_binary_fails = fails;
}

void led_indicator_host_set_create_task_fails(bool fails) {
    host_create_task_fails = fails;
}

void led_indicator_host_set_timer_config_result(esp_err_t result) {
    host_timer_config_result = result;
}

void led_indicator_host_set_channel_config_result(esp_err_t result) {
    host_channel_config_result = result;
}

void led_indicator_host_set_set_duty_result(esp_err_t result) {
    host_set_duty_result = result;
}

void led_indicator_host_set_update_duty_result(esp_err_t result) {
    host_update_duty_result = result;
}

uint32_t led_indicator_host_last_duty(void) {
    return host_last_duty;
}

ledc_timer_config_t led_indicator_host_last_timer_config(void) {
    return host_last_timer_config;
}

ledc_channel_config_t led_indicator_host_last_channel_config(void) {
    return host_last_channel_config;
}

int led_indicator_host_timer_config_calls(void) {
    return host_timer_config_calls;
}

int led_indicator_host_channel_config_calls(void) {
    return host_channel_config_calls;
}

int led_indicator_host_set_duty_calls(void) {
    return host_set_duty_calls;
}

int led_indicator_host_update_duty_calls(void) {
    return host_update_duty_calls;
}

int led_indicator_host_stop_calls(void) {
    return host_stop_calls;
}

int led_indicator_host_timer_pause_calls(void) {
    return host_timer_pause_calls;
}

int led_indicator_host_semaphore_delete_calls(void) {
    return host_semaphore_delete_calls;
}

int led_indicator_host_task_create_calls(void) {
    return host_task_create_calls;
}
