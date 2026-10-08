#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "parent_control_runtime.h"
#include "parent_policy.h"
#include "usage_ledger.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure decision inputs; no ESP-IDF dependency for host tests. */
typedef struct {
    const parent_policy_snapshot_t *policy;
    const char *category;
    bool safety_exempt;
    int32_t minute_of_day;
    bool time_trusted;
    bool daily_limit_reached;
} parent_control_decision_input_t;

/** @brief Evaluate one request against an already resolved policy snapshot. */
parent_control_reason_t parent_control_decision_evaluate(
    const parent_control_decision_input_t *input
);

/**
 * @brief Decide whether a consumption request must be denied.
 *
 * Convenience wrapper used by every gate so host tests can exercise the same
 * decision the firmware applies without linking the ESP-IDF runtime.
 */
bool parent_control_decision_denies(
    const parent_control_decision_input_t *input
);

#ifdef __cplusplus
}
#endif
