#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Normalized quality band shared with the runtime status contract. */
typedef enum {
    NETWORK_QUALITY_UNKNOWN = 0,
    NETWORK_QUALITY_POOR,
    NETWORK_QUALITY_FAIR,
    NETWORK_QUALITY_GOOD,
    NETWORK_QUALITY_EXCELLENT,
} network_quality_level_t;

/** @brief One bounded quality snapshot suitable for a heartbeat payload. */
typedef struct {
    network_quality_level_t level;
    int rssi_dbm;
    int latency_ms;
    int packet_loss_percent;
    bool has_measurement;
} network_quality_snapshot_t;

/**
 * @brief Initialize periodic quality measurement.
 *
 * The module starts without a connected network and records an unknown
 * snapshot until the first successful measurement.
 */
esp_err_t network_quality_init(void);

/** @brief Return the latest bounded quality snapshot. */
network_quality_snapshot_t network_quality_get_snapshot(void);

/** @brief Return the stable contract string for one quality level. */
const char *network_quality_level_name(network_quality_level_t level);

/** @brief Return the removable-module descriptor for network_quality. */
const module_descriptor_t *network_quality_module_descriptor(void);

#ifdef __cplusplus
}
#endif
