#pragma once

#include <stdint.h>

#include "cellular_4g.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure monthly usage accounting for the data quota. */
typedef struct {
    uint32_t billing_day_key;
    uint32_t used_mb;
} cellular_usage_t;

/** @brief Return the UTC day key (days since epoch) for an epoch second. */
uint32_t cellular_day_key(uint32_t epoch_seconds);

/** @brief Reset a usage tracker. */
void cellular_usage_init(cellular_usage_t *usage);

/**
 * @brief Add consumed megabytes and roll the billing window on a new day.
 *
 * The firmware only tracks the running month; a cheap day key is enough to
 * detect the first sample of a new UTC day and reset the counter at the
 * boundary the platform also uses.
 */
void cellular_usage_add_mb(
    cellular_usage_t *usage,
    uint32_t megabytes,
    uint32_t now_epoch_seconds
);

/** @brief Return true when the tracker exhausted the policy quota. */
bool cellular_usage_quota_exhausted(
    const cellular_usage_t *usage,
    const cellular_policy_t *policy,
    uint32_t now_epoch_seconds
);

#ifdef __cplusplus
}
#endif
