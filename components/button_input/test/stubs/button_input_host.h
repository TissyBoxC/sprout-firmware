#pragma once

#include <stddef.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

void button_input_host_reset(void);
void button_input_host_reset_state(void);
void button_input_host_step(void);
void button_input_host_set_tick_count(TickType_t ticks);
void button_input_host_advance_ticks(TickType_t delta);
void button_input_host_set_gpio_level(gpio_num_t gpio, int level);

void button_input_host_set_create_mutex_result(int ok);
void button_input_host_set_create_binary_result(int ok);
void button_input_host_set_task_create_result(int ok);
void button_input_host_set_isr_service_result(esp_err_t result);
void button_input_host_fail_gpio_config_at(int call_number);
void button_input_host_fail_handler_add_at(int call_number);

int button_input_host_gpio_config_calls(void);
gpio_config_t button_input_host_gpio_config_at(size_t index);
int button_input_host_handler_add_calls(void);
int button_input_host_handler_remove_calls(void);
int button_input_host_intr_disable_calls(void);
int button_input_host_gpio_reset_calls(void);
int button_input_host_task_create_calls(void);
int button_input_host_semaphore_delete_calls(void);
int button_input_host_isr_install_calls(void);
int button_input_host_isr_uninstall_calls(void);
bool button_input_host_isr_service_installed(void);
bool button_input_host_task_registered(void);
