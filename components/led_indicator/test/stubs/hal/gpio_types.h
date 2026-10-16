#pragma once

#include <stdbool.h>

/* Host control point for the board GPIO check. */
bool led_indicator_host_gpio_is_valid(int gpio);

#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) led_indicator_host_gpio_is_valid(gpio)
