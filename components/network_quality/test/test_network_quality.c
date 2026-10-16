#include "network_quality.h"

#include "esp_wifi.h"
#include "network_manager.h"

#include <assert.h>
#include <string.h>

void network_quality_host_set_network_state(network_manager_state_t state);
void network_quality_host_emit_network_state(network_manager_state_t state);
void network_quality_host_set_ap_info_result(esp_err_t result);
void network_quality_host_set_rssi(int8_t rssi);
void network_quality_host_run_timer(void);

static void test_level_names(void) {
    assert(strcmp(network_quality_level_name(NETWORK_QUALITY_UNKNOWN), "unknown") == 0);
    assert(strcmp(network_quality_level_name(NETWORK_QUALITY_POOR), "poor") == 0);
    assert(strcmp(network_quality_level_name(NETWORK_QUALITY_FAIR), "fair") == 0);
    assert(strcmp(network_quality_level_name(NETWORK_QUALITY_GOOD), "good") == 0);
    assert(
        strcmp(
            network_quality_level_name(NETWORK_QUALITY_EXCELLENT),
            "excellent"
        ) == 0
    );
    /* An out-of-contract value must fall back to unknown. */
    assert(
        strcmp(
            network_quality_level_name((network_quality_level_t)99),
            "unknown"
        ) == 0
    );
}

static void test_init_and_offline_state(void) {
    network_quality_snapshot_t snapshot;

    network_quality_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(network_quality_init() == ESP_OK);
    /* Idempotent: a second init does not create another timer. */
    assert(network_quality_init() == ESP_OK);

    /* With no connection the snapshot is unknown and reports no measurement. */
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_UNKNOWN);
    assert(!snapshot.has_measurement);
}

static void test_rssi_bands(void) {
    network_quality_snapshot_t snapshot;

    network_quality_host_emit_network_state(NETWORK_MANAGER_STATE_CONNECTED);

    /* Thresholds: poor <= -80, fair <= -65, good <= -50, else excellent. */
    network_quality_host_set_ap_info_result(ESP_OK);

    network_quality_host_set_rssi(-90);
    network_quality_host_run_timer();
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_POOR);
    assert(snapshot.has_measurement);
    assert(snapshot.rssi_dbm == -90);

    network_quality_host_set_rssi(-80);
    network_quality_host_run_timer();
    assert(network_quality_get_snapshot().level == NETWORK_QUALITY_POOR);

    network_quality_host_set_rssi(-79);
    network_quality_host_run_timer();
    assert(network_quality_get_snapshot().level == NETWORK_QUALITY_FAIR);

    network_quality_host_set_rssi(-65);
    network_quality_host_run_timer();
    assert(network_quality_get_snapshot().level == NETWORK_QUALITY_FAIR);

    network_quality_host_set_rssi(-64);
    network_quality_host_run_timer();
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_GOOD);

    network_quality_host_set_rssi(-50);
    network_quality_host_run_timer();
    assert(network_quality_get_snapshot().level == NETWORK_QUALITY_GOOD);

    network_quality_host_set_rssi(-40);
    network_quality_host_run_timer();
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_EXCELLENT);
    /* Fields the hardware cannot provide stay honest at zero. */
    assert(snapshot.latency_ms == 0);
    assert(snapshot.packet_loss_percent == 0);
}

static void test_out_of_range_and_failed_readings_are_unknown(void) {
    network_quality_snapshot_t snapshot;

    /* A positive RSSI is not a real WiFi reading and must be rejected. */
    network_quality_host_set_rssi(10);
    network_quality_host_run_timer();
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_UNKNOWN);
    assert(!snapshot.has_measurement);

    /* The driver can fail while the link is nominally up. */
    network_quality_host_set_ap_info_result(ESP_FAIL);
    network_quality_host_run_timer();
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_UNKNOWN);
    assert(!snapshot.has_measurement);

    /* A drop clears the previous measurement. */
    network_quality_host_set_ap_info_result(ESP_OK);
    network_quality_host_set_rssi(-55);
    network_quality_host_run_timer();
    assert(network_quality_get_snapshot().has_measurement);

    network_quality_host_emit_network_state(NETWORK_MANAGER_STATE_IDLE);
    snapshot = network_quality_get_snapshot();
    assert(snapshot.level == NETWORK_QUALITY_UNKNOWN);
    assert(!snapshot.has_measurement);
}

static void test_module_descriptor_is_stable(void) {
    const module_descriptor_t *descriptor =
        network_quality_module_descriptor();
    assert(descriptor != NULL);
    assert(strcmp(descriptor->module_name, "network_quality") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != NULL);
    assert(descriptor->shutdown == NULL);
}

int main(void) {
    test_level_names();
    test_init_and_offline_state();
    test_rssi_bands();
    test_out_of_range_and_failed_readings_are_unknown();
    test_module_descriptor_is_stable();
    return 0;
}
