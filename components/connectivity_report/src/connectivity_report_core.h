#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The active network bearer class, matching the platform contract. */
typedef enum {
    CONNECTIVITY_BEARER_NONE = 0,
    CONNECTIVITY_BEARER_WIFI,
    CONNECTIVITY_BEARER_CELLULAR,
} connectivity_bearer_t;

/** @brief One bounded connectivity status ready to report. */
typedef struct {
    connectivity_bearer_t bearer;
    bool roaming;
    int signal_percent;
} connectivity_status_t;

/** @brief Return the stable platform string for one bearer. */
const char *connectivity_bearer_name(connectivity_bearer_t bearer);

/**
 * @brief Normalize a Wi-Fi RSSI reading into a bounded 0..100 signal percentage.
 *
 * The mapping runs from -100 dBm (0%) to -50 dBm (100%), which is the band the
 * platform and the guardian UI expect. A reading outside that band clamps into
 * range, and a missing measurement reports 0 so a fresh device never claims a
 * strong signal it has not measured.
 */
int connectivity_signal_percent_from_rssi(int rssi_dbm, bool has_measurement);

/**
 * @brief Build a bounded connectivity status from the live link state.
 *
 * A connected Wi-Fi station reports the Wi-Fi bearer; a disconnected device
 * reports the none bearer with a zero signal, so an offline device converges
 * instead of leaving a stale cellular claim behind.
 */
connectivity_status_t connectivity_report_build(
    bool wifi_connected,
    int rssi_dbm,
    bool has_measurement
);

#ifdef __cplusplus
}
#endif
