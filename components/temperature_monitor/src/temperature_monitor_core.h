#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Normalized thermal bands, mirrored by temperature_state_t. The core stays
 * free of ESP-IDF dependencies so the classifier is covered by host tests. */
typedef enum {
    TEMPERATURE_CORE_UNKNOWN = 0,
    TEMPERATURE_CORE_NORMAL,
    TEMPERATURE_CORE_WARM,
    TEMPERATURE_CORE_HOT,
    TEMPERATURE_CORE_CRITICAL,
} temperature_core_state_t;

/* Inclusive lower bounds, in tenths of a degree Celsius, for each band above
 * normal. Values below the warm threshold classify as normal. */
typedef struct {
    int warm_tenths;
    int hot_tenths;
    int critical_tenths;
} temperature_core_thresholds_t;

/* Contract bounds shared with the platform so out-of-range readings are
 * rejected rather than stored. */
#define TEMPERATURE_CORE_MIN_TENTHS (-400)
#define TEMPERATURE_CORE_MAX_TENTHS 1250

/** @brief Classify a reading into a band; out-of-range readings are unknown. */
temperature_core_state_t temperature_core_classify(
    int celsius_tenths,
    const temperature_core_thresholds_t *thresholds
);

/** @brief Stable contract string for one band. */
const char *temperature_core_state_name(temperature_core_state_t state);

#ifdef __cplusplus
}
#endif
