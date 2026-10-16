#pragma once

#include <stdbool.h>
#include <stdint.h>

bool volume_control_is_ready(void);
uint8_t volume_control_get_percent(void);
uint8_t volume_control_get_max_percent(void);
bool volume_control_is_muted(void);
