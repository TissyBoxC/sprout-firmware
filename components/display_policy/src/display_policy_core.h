#pragma once

#include <stdint.h>

#include "display_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure daily usage accounting. */
typedef struct {
    uint32_t day_key;
    uint32_t used_seconds;
    uint32_t break_window_started_at;
    bool break_window_active;
} display_policy_usage_t;

/** @brief Return the UTC day key (days since epoch) for an epoch second. */
uint32_t display_policy_day_key(uint32_t epoch_seconds);

/** @brief Reset a usage tracker. */
void display_policy_usage_init(display_policy_usage_t *usage);

/**
 * @brief Account one active second and roll the daily window when the day
 * changes.
 */
void display_policy_usage_add_second(
    display_policy_usage_t *usage,
    uint32_t now_epoch_seconds
);

/** @brief Begin a continuous-use window. */
void display_policy_usage_start_break_window(
    display_policy_usage_t *usage,
    uint32_t now_epoch_seconds
);

/** @brief Return true when the break interval elapsed since the window start. */
bool display_policy_usage_break_due(
    const display_policy_usage_t *usage,
    uint16_t break_interval_minutes,
    uint32_t now_epoch_seconds
);

/** @brief Return true when the stored daily usage reached the limit. */
bool display_policy_usage_daily_limit_reached(
    const display_policy_usage_t *usage,
    uint16_t daily_limit_minutes,
    uint32_t now_epoch_seconds
);

#ifdef __cplusplus
}
#endif
