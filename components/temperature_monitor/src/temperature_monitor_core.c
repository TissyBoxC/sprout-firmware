#include "temperature_monitor_core.h"

#include <stddef.h>

temperature_core_state_t temperature_core_classify(
    int celsius_tenths,
    const temperature_core_thresholds_t *thresholds
) {
    if (thresholds == NULL) {
        return TEMPERATURE_CORE_UNKNOWN;
    }
    if (celsius_tenths < TEMPERATURE_CORE_MIN_TENTHS ||
        celsius_tenths > TEMPERATURE_CORE_MAX_TENTHS) {
        return TEMPERATURE_CORE_UNKNOWN;
    }
    if (celsius_tenths >= thresholds->critical_tenths) {
        return TEMPERATURE_CORE_CRITICAL;
    }
    if (celsius_tenths >= thresholds->hot_tenths) {
        return TEMPERATURE_CORE_HOT;
    }
    if (celsius_tenths >= thresholds->warm_tenths) {
        return TEMPERATURE_CORE_WARM;
    }
    return TEMPERATURE_CORE_NORMAL;
}

const char *temperature_core_state_name(temperature_core_state_t state) {
    switch (state) {
        case TEMPERATURE_CORE_NORMAL:
            return "normal";
        case TEMPERATURE_CORE_WARM:
            return "warm";
        case TEMPERATURE_CORE_HOT:
            return "hot";
        case TEMPERATURE_CORE_CRITICAL:
            return "critical";
        case TEMPERATURE_CORE_UNKNOWN:
        default:
            return "unknown";
    }
}
