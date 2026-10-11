#pragma once

#include <stdint.h>

#include "battery_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pure energy accounting helpers used by the estimation path.
 *
 * The estimator only needs a small moving average of the bounded percentage
 * and the charge direction, so the module keeps a tiny fixed window that runs
 * identically in host tests and on the device.
 */
typedef struct {
    uint32_t sample_count;
    uint32_t sample_sum;
} battery_average_t;

/** @brief Reset an average accumulator. */
void battery_average_init(battery_average_t *average);

/** @brief Add one bounded percentage sample and return the running average. */
uint8_t battery_average_add(battery_average_t *average, uint8_t percent);

/**
 * @brief Derive a charge state from the charger pin and a full threshold.
 *
 * `charging` is the raw charger-presence flag; `full` is the voltage-based full
 * detection. A present charger with a full pack reports FULL, a present charger
 * below the full threshold reports CHARGING, and an absent charger reports
 * DISCHARGING.
 */
battery_charge_state_t battery_derive_charge_state(
    bool charging,
    bool full
);

#ifdef __cplusplus
}
#endif
