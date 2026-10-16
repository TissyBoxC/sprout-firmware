#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef int gpio_num_t;

#define GPIO_NUM_NC ((gpio_num_t)-1)

typedef enum {
    GPIO_MODE_INPUT = 0,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE = 1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE = 1,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
    GPIO_INTR_ANYEDGE = 3,
} gpio_intr_type_t;

typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_intr_type_t intr_type;
} gpio_config_t;

typedef void (*gpio_isr_t)(void *argument);

esp_err_t gpio_config(const gpio_config_t *config);
int gpio_get_level(gpio_num_t gpio);
esp_err_t gpio_install_isr_service(int intr_alloc_flags);
void gpio_uninstall_isr_service(void);
esp_err_t gpio_isr_handler_add(
    gpio_num_t gpio,
    gpio_isr_t isr_handler,
    void *argument
);
esp_err_t gpio_isr_handler_remove(gpio_num_t gpio);
esp_err_t gpio_set_intr_type(gpio_num_t gpio, gpio_intr_type_t intr_type);
esp_err_t gpio_intr_enable(gpio_num_t gpio);
esp_err_t gpio_intr_disable(gpio_num_t gpio);
esp_err_t gpio_reset_pin(gpio_num_t gpio);
esp_err_t gpio_set_direction(gpio_num_t gpio, gpio_mode_t mode);
