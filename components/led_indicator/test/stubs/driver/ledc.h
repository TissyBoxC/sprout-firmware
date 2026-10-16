#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    LEDC_LOW_SPEED_MODE = 0,
    LEDC_HIGH_SPEED_MODE,
} ledc_mode_t;

typedef enum {
    LEDC_TIMER_0 = 0,
    LEDC_TIMER_1,
} ledc_timer_t;

typedef enum {
    LEDC_CHANNEL_0 = 0,
    LEDC_CHANNEL_1,
} ledc_channel_t;

typedef enum {
    LEDC_TIMER_10_BIT = 10,
} ledc_timer_bit_t;

typedef enum {
    LEDC_AUTO_CLK = 0,
} ledc_clk_cfg_t;

typedef enum {
    LEDC_SLEEP_MODE_NO_ALIVE_NO_PD = 0,
} ledc_sleep_mode_t;

/* Field order mirrors ESP-IDF: the module uses designated initialisers, which
   must follow declaration order under -Werror. */
typedef struct {
    bool output_invert;
} ledc_channel_flags_t;

typedef struct {
    ledc_mode_t speed_mode;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t timer_num;
    uint32_t freq_hz;
    ledc_clk_cfg_t clk_cfg;
    bool deconfigure;
} ledc_timer_config_t;

typedef struct {
    int gpio_num;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    int intr_type;
    ledc_timer_t timer_sel;
    uint32_t duty;
    int hpoint;
    ledc_sleep_mode_t sleep_mode;
    ledc_channel_flags_t flags;
    bool deconfigure;
} ledc_channel_config_t;

esp_err_t ledc_timer_config(const ledc_timer_config_t *config);
esp_err_t ledc_channel_config(const ledc_channel_config_t *config);
esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty);
esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel);
esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level);
esp_err_t ledc_timer_pause(ledc_mode_t speed_mode, ledc_timer_t timer_num);
