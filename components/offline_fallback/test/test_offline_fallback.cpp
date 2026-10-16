#include "offline_fallback.h"

#include "config_store.h"
#include "network_manager.h"
#include "time_sync.h"

#include <cassert>

void offline_host_reset_store(bool ready);
void offline_host_put(const char *key, const char *value);
void offline_host_set_store_write_result(esp_err_t result);
void offline_host_set_network_state(network_manager_state_t state);
void offline_host_emit_network_state(network_manager_state_t state);
void offline_host_set_time_synchronized(bool synchronized);
int offline_host_timer_starts(void);
int offline_host_timer_stops(void);
int offline_host_timer_creates(void);
void offline_host_set_timer_create_result(esp_err_t result);

namespace {

void test_init_requires_a_ready_store() {
    offline_host_reset_store(false);
    assert(offline_fallback_init() == ESP_ERR_INVALID_STATE);
}

void test_online_boot_starts_clean() {
    offline_host_reset_store(true);
    offline_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    offline_host_set_timer_create_result(ESP_OK);
    assert(offline_fallback_init() == ESP_OK);
    // Idempotent: no second timer is created.
    const int creates = offline_host_timer_creates();
    assert(offline_fallback_init() == ESP_OK);
    assert(offline_host_timer_creates() == creates);

    const offline_fallback_snapshot_t snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_ONLINE);
    assert(snapshot.reason == OFFLINE_REASON_NONE);
    assert(!snapshot.fallback_active);
    assert(snapshot.pending_telemetry == 0u);

    // A boot-time callback with no prior loss must not report a reconnect.
    assert(!offline_fallback_consume_network_reconnected());
}

void test_network_loss_enters_grace_and_starts_the_countdown() {
    const int starts_before = offline_host_timer_starts();
    offline_host_emit_network_state(NETWORK_MANAGER_STATE_IDLE);

    const offline_fallback_snapshot_t snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_GRACE);
    assert(snapshot.reason == OFFLINE_REASON_NETWORK_UNAVAILABLE);
    assert(!snapshot.fallback_active);
    assert(offline_host_timer_starts() == starts_before + 1);

    // The loss is persisted so a reboot during the outage keeps the flag.
    assert(!offline_fallback_consume_network_reconnected());
}

void test_reconnect_reports_reconnected_once() {
    offline_host_emit_network_state(NETWORK_MANAGER_STATE_CONNECTED);

    const offline_fallback_snapshot_t snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_GRACE);
    assert(snapshot.reason == OFFLINE_REASON_SERVICE_UNAVAILABLE);

    // The reconnect event is delivered exactly once.
    assert(offline_fallback_consume_network_reconnected());
    assert(!offline_fallback_consume_network_reconnected());
}

void test_pending_telemetry_accumulates_and_clears() {
    const int stops_before = offline_host_timer_stops();
    assert(offline_fallback_record_pending_telemetry() == ESP_OK);
    assert(offline_fallback_record_pending_telemetry() == ESP_OK);
    assert(offline_fallback_get_snapshot().pending_telemetry == 2u);

    // A failed clear must keep the in-memory backlog intact.
    offline_host_set_store_write_result(ESP_FAIL);
    assert(offline_fallback_clear_pending_telemetry() == ESP_FAIL);
    assert(offline_fallback_get_snapshot().pending_telemetry == 2u);

    offline_host_set_store_write_result(ESP_OK);
    assert(offline_fallback_clear_pending_telemetry() == ESP_OK);
    assert(offline_fallback_get_snapshot().pending_telemetry == 0u);
    assert(stops_before == offline_host_timer_stops());
}

void test_service_and_time_failures_use_grace_with_distinct_reasons() {
    assert(offline_fallback_mark_service_unavailable() == ESP_OK);
    offline_fallback_snapshot_t snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_GRACE);
    assert(snapshot.reason == OFFLINE_REASON_SERVICE_UNAVAILABLE);

    assert(offline_fallback_mark_time_unsynchronized() == ESP_OK);
    snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_GRACE);
    assert(snapshot.reason == OFFLINE_REASON_TIME_NOT_SYNCHRONIZED);
}

void test_recovery_requires_a_synchronized_clock() {
    // An unsynchronized clock keeps the device in grace even after recovery.
    offline_host_set_time_synchronized(false);
    assert(offline_fallback_mark_recovered() == ESP_OK);
    offline_fallback_snapshot_t snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_GRACE);
    assert(snapshot.reason == OFFLINE_REASON_TIME_NOT_SYNCHRONIZED);

    offline_host_set_time_synchronized(true);
    assert(offline_fallback_mark_recovered() == ESP_OK);
    snapshot = offline_fallback_get_snapshot();
    assert(snapshot.state == OFFLINE_FALLBACK_STATE_ONLINE);
    assert(snapshot.reason == OFFLINE_REASON_NONE);
    assert(!snapshot.fallback_active);
}

void test_pending_telemetry_is_bounded() {
    // Fill to the bound, then confirm further records are accepted but do not
    // overflow the bounded counter.
    for (int index = 0; index < 10001; ++index) {
        assert(offline_fallback_record_pending_telemetry() == ESP_OK);
    }
    assert(offline_fallback_get_snapshot().pending_telemetry == 10000u);
    assert(offline_fallback_clear_pending_telemetry() == ESP_OK);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        offline_fallback_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->module_name != nullptr);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_init_requires_a_ready_store();
    test_online_boot_starts_clean();
    test_network_loss_enters_grace_and_starts_the_countdown();
    test_reconnect_reports_reconnected_once();
    test_pending_telemetry_accumulates_and_clears();
    test_service_and_time_failures_use_grace_with_distinct_reasons();
    test_recovery_requires_a_synchronized_clock();
    test_pending_telemetry_is_bounded();
    test_module_descriptor_is_stable();
    return 0;
}
