#include "time_sync.h"

#include <cassert>
#include <cstring>

#include "esp_sntp.h"
#include "network_manager.h"
#include "time_sync_host.h"

namespace {

constexpr int64_t kMinimumEpoch = 1704067200;
constexpr int64_t kCurrentEpoch = 1791475200;

void reset_all() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    assert(time_sync_init() == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor = time_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "time_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
}

void test_init_configures_sntp_and_network_observer() {
    reset_all();
    assert(time_sync_is_ready());
    assert(time_sync_get_state() == TIME_SYNC_STATE_UNSYNCHRONIZED);
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_NONE);
    assert(time_sync_get_last_synced_epoch() == 0);
    assert(time_sync_host_sntp_init_calls() == 1);
    assert(time_sync_host_sntp_mode() == ESP_SNTP_OPMODE_POLL);
    assert(std::strcmp(time_sync_host_sntp_server(), "ntp.aliyun.com") == 0);
}

void test_init_rejects_unready_store_and_propagates_observer_failure() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    time_sync_host_set_store_ready(false);
    assert(time_sync_init() == ESP_ERR_INVALID_STATE);
    assert(!time_sync_is_ready());
    assert(time_sync_host_sntp_init_calls() == 0);

}

void test_restored_time_is_installed_before_network() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    time_sync_host_set_string("time_trusted", "1");
    time_sync_host_set_string("time_synced_at", "1791475200");
    time_sync_host_set_string("time_offset_ms", "-25");
    assert(time_sync_init() == ESP_OK);
    assert(time_sync_is_synchronized());
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_PLATFORM);
    assert(time_sync_get_last_synced_epoch() == kCurrentEpoch);
    assert(time_sync_get_offset_ms() == -25);
    assert(time_sync_host_clock_epoch() == kCurrentEpoch);
    assert(time_sync_host_clock_calls() == 1);
}

void test_invalid_persisted_time_is_not_trusted() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    time_sync_host_set_string("time_trusted", "1");
    time_sync_host_set_string("time_synced_at", "123");
    assert(time_sync_init() == ESP_OK);
    assert(!time_sync_is_synchronized());
    assert(time_sync_host_clock_calls() == 0);
}

void test_connected_network_starts_sntp_only_when_unsynchronized() {
    reset_all();
    assert(time_sync_host_fire_network_state(
               NETWORK_MANAGER_STATE_CONNECTED
           ) == ESP_OK);
    assert(time_sync_get_state() == TIME_SYNC_STATE_SYNCHRONIZING);
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_SNTP);
    assert(time_sync_host_sntp_restart_calls() == 1);

    time_sync_host_fire_sntp(kCurrentEpoch);
    assert(time_sync_is_synchronized());
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_SNTP);
    assert(time_sync_get_last_synced_epoch() == kCurrentEpoch);
    assert(time_sync_host_sntp_status() == SNTP_SYNC_STATUS_COMPLETED);

    time_sync_host_fire_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    assert(time_sync_host_sntp_restart_calls() == 1);
}

void test_sntp_rejects_implausible_time() {
    reset_all();
    time_sync_host_fire_sntp(kMinimumEpoch - 1);
    assert(!time_sync_is_synchronized());
    assert(time_sync_host_clock_calls() == 0);
}

void test_platform_time_sets_clock_and_persists() {
    reset_all();
    assert(time_sync_accept_platform_time(kCurrentEpoch) == ESP_OK);
    assert(time_sync_is_synchronized());
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_PLATFORM);
    assert(time_sync_get_last_synced_epoch() == kCurrentEpoch);
    assert(time_sync_get_offset_ms() == 0);
    assert(time_sync_host_clock_epoch() == kCurrentEpoch);
    assert(std::strcmp(time_sync_host_string("time_trusted"), "1") == 0);
    assert(std::strcmp(time_sync_host_string("time_synced_at"), "1791475200") == 0);
    assert(std::strcmp(time_sync_host_string("time_offset_ms"), "0") == 0);
}

void test_platform_time_rejects_implausible_value() {
    reset_all();
    assert(time_sync_accept_platform_time(kMinimumEpoch - 1) ==
           ESP_ERR_INVALID_ARG);
    assert(!time_sync_is_synchronized());
    assert(time_sync_host_clock_calls() == 0);
}

void test_platform_time_requires_initialized_module() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    assert(time_sync_accept_platform_time(kCurrentEpoch) ==
           ESP_ERR_INVALID_STATE);
    assert(time_sync_host_clock_calls() == 0);
}

void test_platform_clock_failure_is_propagated() {
    reset_all();
    time_sync_host_set_clock_result(-1);
    assert(time_sync_accept_platform_time(kCurrentEpoch) ==
           ESP_ERR_INVALID_STATE);
    assert(!time_sync_is_synchronized());
    assert(time_sync_host_clock_calls() == 1);
    assert(std::strcmp(time_sync_host_string("time_trusted"), "") == 0);
}

void test_platform_persistence_failure_is_propagated() {
    reset_all();
    time_sync_host_set_set_result("time_synced_at", ESP_ERR_TIMEOUT);
    assert(time_sync_accept_platform_time(kCurrentEpoch) ==
           ESP_ERR_INVALID_STATE);
    assert(time_sync_is_synchronized());
    assert(time_sync_host_clock_epoch() == kCurrentEpoch);
    assert(std::strcmp(time_sync_host_string("time_trusted"), "") == 0);

    reset_all();
    time_sync_host_set_set_result("time_trusted", ESP_ERR_TIMEOUT);
    assert(time_sync_accept_platform_time(kCurrentEpoch) ==
           ESP_ERR_INVALID_STATE);
    assert(time_sync_is_synchronized());
    assert(time_sync_host_clock_epoch() == kCurrentEpoch);
}

void test_restored_time_requires_working_system_clock() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    time_sync_host_set_string("time_trusted", "1");
    time_sync_host_set_string("time_synced_at", "1791475200");
    time_sync_host_set_clock_result(-1);
    assert(time_sync_init() == ESP_OK);
    assert(!time_sync_is_synchronized());
    assert(time_sync_get_last_synced_epoch() == 0);
    assert(time_sync_get_offset_ms() == 0);
}

void test_resynchronize_checks_ready_and_network() {
    time_sync_host_reset();
    time_sync_host_reset_module();
    assert(time_sync_resynchronize() == ESP_ERR_INVALID_STATE);

    reset_all();
    time_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(time_sync_resynchronize() == ESP_ERR_INVALID_STATE);

    time_sync_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    assert(time_sync_resynchronize() == ESP_OK);
    assert(time_sync_get_state() == TIME_SYNC_STATE_SYNCHRONIZING);
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_SNTP);
    assert(time_sync_host_sntp_restart_calls() == 1);
}

void test_state_callback_receives_transitions() {
    reset_all();
    int calls = 0;
    time_sync_state_t state = TIME_SYNC_STATE_UNSYNCHRONIZED;
    time_sync_source_t source = TIME_SYNC_SOURCE_NONE;
    assert(time_sync_set_state_callback(
               [](time_sync_state_t next_state,
                  time_sync_source_t next_source,
                  void *context) {
                   int *count = (int *)context;
                   *count += 1;
                   (void)next_state;
                   (void)next_source;
               },
               &calls
           ) == ESP_OK);
    time_sync_host_fire_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    assert(calls == 1);
    time_sync_host_fire_sntp(kCurrentEpoch);
    assert(calls == 2);
    assert(time_sync_get_state() == TIME_SYNC_STATE_SYNCHRONIZED);
    assert(time_sync_get_source() == TIME_SYNC_SOURCE_SNTP);
    (void)state;
    (void)source;
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_init_configures_sntp_and_network_observer();
    test_init_rejects_unready_store_and_propagates_observer_failure();
    test_restored_time_is_installed_before_network();
    test_invalid_persisted_time_is_not_trusted();
    test_connected_network_starts_sntp_only_when_unsynchronized();
    test_sntp_rejects_implausible_time();
    test_platform_time_sets_clock_and_persists();
    test_platform_time_rejects_implausible_value();
    test_platform_time_requires_initialized_module();
    test_platform_clock_failure_is_propagated();
    test_platform_persistence_failure_is_propagated();
    test_restored_time_requires_working_system_clock();
    test_resynchronize_checks_ready_and_network();
    test_state_callback_receives_transitions();
    return 0;
}
