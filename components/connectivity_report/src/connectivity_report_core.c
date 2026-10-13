#include "connectivity_report_core.h"

const char *connectivity_bearer_name(connectivity_bearer_t bearer) {
    switch (bearer) {
        case CONNECTIVITY_BEARER_WIFI:
            return "wifi";
        case CONNECTIVITY_BEARER_CELLULAR:
            return "cellular";
        case CONNECTIVITY_BEARER_NONE:
        default:
            return "none";
    }
}

int connectivity_signal_percent_from_rssi(int rssi_dbm, bool has_measurement) {
    if (!has_measurement) {
        return 0;
    }
    // -100 dBm maps to 0% and -50 dBm maps to 100%. Clamp outside the band.
    const int weak_dbm = -100;
    const int strong_dbm = -50;
    if (rssi_dbm <= weak_dbm) {
        return 0;
    }
    if (rssi_dbm >= strong_dbm) {
        return 100;
    }
    return (rssi_dbm - weak_dbm) * 100 / (strong_dbm - weak_dbm);
}

connectivity_status_t connectivity_report_build(
    bool wifi_connected,
    int rssi_dbm,
    bool has_measurement
) {
    connectivity_status_t status = {
        .bearer = CONNECTIVITY_BEARER_NONE,
        .roaming = false,
        .signal_percent = 0,
    };
    if (!wifi_connected) {
        return status;
    }
    status.bearer = CONNECTIVITY_BEARER_WIFI;
    status.signal_percent =
        connectivity_signal_percent_from_rssi(rssi_dbm, has_measurement);
    return status;
}
