#include "connectivity_report_core.h"

#include <cassert>
#include <cstring>

int main() {
    // RSSI maps linearly from -100 dBm (0%) to -50 dBm (100%).
    assert(connectivity_signal_percent_from_rssi(-100, true) == 0);
    assert(connectivity_signal_percent_from_rssi(-50, true) == 100);
    assert(connectivity_signal_percent_from_rssi(-75, true) == 50);
    assert(connectivity_signal_percent_from_rssi(-60, true) == 80);
    assert(connectivity_signal_percent_from_rssi(-90, true) == 20);

    // Readings outside the band clamp into range instead of overflowing.
    assert(connectivity_signal_percent_from_rssi(-120, true) == 0);
    assert(connectivity_signal_percent_from_rssi(-30, true) == 100);
    assert(connectivity_signal_percent_from_rssi(0, true) == 100);

    // A missing measurement reports 0 so a fresh device never claims a strong
    // signal it has not actually measured.
    assert(connectivity_signal_percent_from_rssi(-50, false) == 0);
    assert(connectivity_signal_percent_from_rssi(0, false) == 0);

    // The bearer strings match the platform contract exactly.
    assert(std::strcmp(connectivity_bearer_name(CONNECTIVITY_BEARER_NONE),
                       "none") == 0);
    assert(std::strcmp(connectivity_bearer_name(CONNECTIVITY_BEARER_WIFI),
                       "wifi") == 0);
    assert(std::strcmp(connectivity_bearer_name(CONNECTIVITY_BEARER_CELLULAR),
                       "cellular") == 0);

    // A connected station reports the Wi-Fi bearer and the measured signal.
    const connectivity_status_t online =
        connectivity_report_build(true, -75, true);
    assert(online.bearer == CONNECTIVITY_BEARER_WIFI);
    assert(online.roaming == false);
    assert(online.signal_percent == 50);

    // A connected station without a fresh measurement still reports Wi-Fi but
    // with a zero signal.
    const connectivity_status_t online_no_measure =
        connectivity_report_build(true, -50, false);
    assert(online_no_measure.bearer == CONNECTIVITY_BEARER_WIFI);
    assert(online_no_measure.signal_percent == 0);

    // A disconnected device converges to the none bearer with no signal, so a
    // stale Wi-Fi claim is never left behind.
    const connectivity_status_t offline =
        connectivity_report_build(false, -50, true);
    assert(offline.bearer == CONNECTIVITY_BEARER_NONE);
    assert(offline.roaming == false);
    assert(offline.signal_percent == 0);

    return 0;
}
