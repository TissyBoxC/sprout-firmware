#include "factory_reset.h"

#include "esp_err.h"

#include <cassert>
#include <cstring>

void factory_reset_host_reset(void);
void factory_reset_host_set_create_mutex_fails(bool fails);
void factory_reset_host_set_config_store_ready(bool ready);
void factory_reset_host_set_config_store_erase_result(esp_err_t result);
void factory_reset_host_set_erase_partition_result(esp_err_t result);
void factory_reset_host_set_init_result(esp_err_t result);
void factory_reset_host_set_timer_create_result(esp_err_t result);
void factory_reset_host_set_timer_start_result(esp_err_t result);
void factory_reset_host_advance_ms(int64_t delta_ms);
void factory_reset_host_fire_timer(void);
int factory_reset_host_erased_key_count(void);
bool factory_reset_host_erased_key(const char *key);
const char *factory_reset_host_last_erased_partition(void);
uint64_t factory_reset_host_last_timer_timeout_us(void);
int factory_reset_host_erase_partition_calls(void);
int factory_reset_host_init_calls(void);
int factory_reset_host_timer_create_calls(void);
int factory_reset_host_timer_start_calls(void);
int factory_reset_host_timer_stop_calls(void);
int factory_reset_host_timer_delete_calls(void);
int factory_reset_host_mutex_delete_calls(void);

namespace {

const module_descriptor_t *descriptor() {
    const module_descriptor_t *value = factory_reset_module_descriptor();
    assert(value != nullptr);
    assert(value->initialize != nullptr);
    assert(value->shutdown != nullptr);
    return value;
}

// Every case starts from a clean, initialised module.
void prepare() {
    descriptor()->shutdown();
    factory_reset_host_reset();
    assert(descriptor()->initialize() == ESP_OK);
}

void test_calls_before_init_are_refused() {
    descriptor()->shutdown();
    factory_reset_host_reset();

    // An invalid reason is rejected before the readiness check.
    assert(
        factory_reset_request(static_cast<factory_reset_reason_t>(99)) ==
        FACTORY_RESET_ERR_INVALID_REASON
    );
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_ERR_NOT_INITIALIZED
    );
    assert(
        factory_reset_confirm() == FACTORY_RESET_ERR_NOT_INITIALIZED
    );
    assert(factory_reset_cancel() == FACTORY_RESET_ERR_NOT_INITIALIZED);
    assert(!factory_reset_is_pending());

    const factory_reset_snapshot_t snapshot = factory_reset_get_snapshot();
    assert(!snapshot.is_ready);
    assert(!snapshot.is_pending);
    assert(
        snapshot.last_result == FACTORY_RESET_ERR_NOT_INITIALIZED
    );
}

void test_init_failures_are_reported_and_cleaned_up() {
    descriptor()->shutdown();
    factory_reset_host_reset();
    factory_reset_host_set_create_mutex_fails(true);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);
    assert(!factory_reset_get_snapshot().is_ready);

    factory_reset_host_reset();
    factory_reset_host_set_timer_create_result(ESP_ERR_INVALID_STATE);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);
    assert(!factory_reset_get_snapshot().is_ready);
    // The mutex created before the timer must not leak.
    assert(factory_reset_host_mutex_delete_calls() == 1);
}

void test_init_is_idempotent_with_one_timer() {
    descriptor()->shutdown();
    factory_reset_host_reset();
    assert(descriptor()->initialize() == ESP_OK);
    assert(descriptor()->initialize() == ESP_OK);
    assert(factory_reset_host_timer_create_calls() == 1);
}

void test_request_validates_reasons() {
    prepare();
    assert(
        factory_reset_request(static_cast<factory_reset_reason_t>(-1)) ==
        FACTORY_RESET_ERR_INVALID_REASON
    );
    assert(
        factory_reset_request(static_cast<factory_reset_reason_t>(4)) ==
        FACTORY_RESET_ERR_INVALID_REASON
    );
    assert(!factory_reset_is_pending());
    assert(factory_reset_host_timer_start_calls() == 0);
}

void test_request_arms_the_guarded_window() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_BUTTON_GESTURE) ==
        FACTORY_RESET_OK
    );
    assert(factory_reset_is_pending());

    const factory_reset_snapshot_t snapshot = factory_reset_get_snapshot();
    assert(snapshot.is_ready);
    assert(snapshot.is_pending);
    assert(snapshot.request_reason == FACTORY_RESET_REASON_BUTTON_GESTURE);
    assert(snapshot.remaining_ms == 10000);
    assert(snapshot.last_result == FACTORY_RESET_OK);
    assert(snapshot.completed_count == 0);

    // The one-shot timer matches the configured 10 second window.
    assert(factory_reset_host_timer_start_calls() == 1);
    assert(factory_reset_host_last_timer_timeout_us() == 10000000ULL);
}

void test_re_request_replaces_reason_and_restarts_the_window() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    factory_reset_host_advance_ms(4000);
    assert(factory_reset_get_snapshot().remaining_ms == 6000);

    // A new request must restart the bounded window rather than fail.
    assert(
        factory_reset_request(FACTORY_RESET_REASON_PROVISIONING_RESET) ==
        FACTORY_RESET_OK
    );
    const factory_reset_snapshot_t snapshot = factory_reset_get_snapshot();
    assert(snapshot.is_pending);
    assert(snapshot.request_reason == FACTORY_RESET_REASON_PROVISIONING_RESET);
    assert(snapshot.remaining_ms == 10000);
    assert(factory_reset_host_timer_start_calls() == 2);
}

void test_timer_start_failure_disarms_the_request() {
    prepare();
    factory_reset_host_set_timer_start_result(ESP_ERR_INVALID_STATE);
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_ERR_STORAGE
    );
    assert(!factory_reset_is_pending());
    assert(
        factory_reset_get_snapshot().last_result ==
        FACTORY_RESET_ERR_STORAGE
    );
}

void test_confirm_without_a_request_is_refused() {
    prepare();
    assert(factory_reset_confirm() == FACTORY_RESET_ERR_NO_PENDING_RESET);
    assert(factory_reset_host_erase_partition_calls() == 0);
    assert(factory_reset_host_erased_key_count() == 0);
}

void test_confirm_erases_config_and_partition() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_INTERNAL_RECOVERY) ==
        FACTORY_RESET_OK
    );
    assert(factory_reset_confirm() == FACTORY_RESET_OK);

    // Every credential-bearing key is erased before the partition erase.
    assert(factory_reset_host_erased_key_count() == 10);
    assert(factory_reset_host_erased_key("wifi_ssid"));
    assert(factory_reset_host_erased_key("wifi_password"));
    assert(factory_reset_host_erased_key("prov_srp_salt"));
    assert(factory_reset_host_erased_key("prov_verifier"));
    assert(factory_reset_host_erased_key("prov_pop"));
    assert(factory_reset_host_erased_key("platform_url"));
    assert(factory_reset_host_erased_key("device_bound"));
    assert(factory_reset_host_erased_key("dev_registered"));
    assert(factory_reset_host_erased_key("device_session"));
    assert(factory_reset_host_erased_key("dev_reg_token"));

    assert(
        std::strcmp(factory_reset_host_last_erased_partition(), "nvs") == 0
    );
    assert(factory_reset_host_erase_partition_calls() == 1);
    // The default partition is re-initialised after the erase.
    assert(factory_reset_host_init_calls() == 1);

    const factory_reset_snapshot_t snapshot = factory_reset_get_snapshot();
    assert(!snapshot.is_pending);
    assert(snapshot.last_result == FACTORY_RESET_OK);
    assert(snapshot.completed_count == 1);
}

void test_confirm_twice_requires_a_new_request() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    assert(factory_reset_confirm() == FACTORY_RESET_OK);
    assert(factory_reset_confirm() == FACTORY_RESET_ERR_NO_PENDING_RESET);
    assert(factory_reset_get_snapshot().completed_count == 1);
}

void test_expired_request_is_rejected_without_erasing() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_BUTTON_GESTURE) ==
        FACTORY_RESET_OK
    );
    factory_reset_host_advance_ms(10000);

    assert(factory_reset_confirm() == FACTORY_RESET_ERR_EXPIRED);
    assert(factory_reset_host_erase_partition_calls() == 0);
    assert(factory_reset_host_erased_key_count() == 0);
    assert(!factory_reset_is_pending());
    assert(
        factory_reset_get_snapshot().last_result == FACTORY_RESET_ERR_EXPIRED
    );
}

void test_pending_accessor_expires_the_request() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    // Just under the window stays armed.
    factory_reset_host_advance_ms(9999);
    assert(factory_reset_is_pending());
    assert(factory_reset_get_snapshot().remaining_ms == 1);

    factory_reset_host_advance_ms(1);
    assert(!factory_reset_is_pending());
    assert(
        factory_reset_get_snapshot().last_result == FACTORY_RESET_ERR_EXPIRED
    );
}

void test_timer_callback_expires_the_request() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_PROVISIONING_RESET) ==
        FACTORY_RESET_OK
    );
    factory_reset_host_advance_ms(10000);
    factory_reset_host_fire_timer();

    assert(!factory_reset_is_pending());
    assert(
        factory_reset_get_snapshot().last_result == FACTORY_RESET_ERR_EXPIRED
    );
}

void test_cancel_disarms_without_erasing() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    const int stops_before = factory_reset_host_timer_stop_calls();
    assert(factory_reset_cancel() == FACTORY_RESET_OK);
    assert(!factory_reset_is_pending());
    assert(factory_reset_host_erase_partition_calls() == 0);
    assert(factory_reset_host_erased_key_count() == 0);
    // Cancel disarms the one-shot exactly once; the request path's own
    // defensive stop is not part of this delta.
    assert(factory_reset_host_timer_stop_calls() == stops_before + 1);
    assert(factory_reset_get_snapshot().last_result == FACTORY_RESET_OK);
}

void test_partition_erase_failure_is_reported_as_storage() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    factory_reset_host_set_erase_partition_result(ESP_FAIL);
    assert(factory_reset_confirm() == FACTORY_RESET_ERR_STORAGE);

    const factory_reset_snapshot_t snapshot = factory_reset_get_snapshot();
    assert(!snapshot.is_pending);
    assert(snapshot.last_result == FACTORY_RESET_ERR_STORAGE);
    assert(snapshot.completed_count == 0);
}

void test_missing_config_store_still_erases_the_partition() {
    prepare();
    factory_reset_host_set_config_store_ready(false);
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    assert(factory_reset_confirm() == FACTORY_RESET_OK);
    // No per-key erases are attempted without a ready config_store.
    assert(factory_reset_host_erased_key_count() == 0);
    assert(factory_reset_host_erase_partition_calls() == 1);
}

void test_uninitialised_nvs_is_tolerated() {
    prepare();
    factory_reset_host_set_erase_partition_result(ESP_ERR_NVS_NOT_INITIALIZED);
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    assert(factory_reset_confirm() == FACTORY_RESET_OK);
    assert(factory_reset_host_init_calls() == 1);
}

void test_stale_nvs_partition_is_erased_and_re_initialised() {
    prepare();
    factory_reset_host_set_init_result(ESP_ERR_NVS_NO_FREE_PAGES);
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );

    // The first erase succeeds, then the partition refuses to init. The
    // module must erase again and retry instead of reporting success.
    assert(factory_reset_confirm() == FACTORY_RESET_ERR_STORAGE);
    assert(factory_reset_host_erase_partition_calls() == 2);
    assert(factory_reset_get_snapshot().completed_count == 0);
}

void test_reason_and_error_names_are_stable() {
    assert(
        std::strcmp(
            factory_reset_reason_name(FACTORY_RESET_REASON_GUARDIAN_REQUEST),
            "guardian_request"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_reason_name(FACTORY_RESET_REASON_BUTTON_GESTURE),
            "button_gesture"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_reason_name(FACTORY_RESET_REASON_PROVISIONING_RESET),
            "provisioning_reset"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_reason_name(FACTORY_RESET_REASON_INTERNAL_RECOVERY),
            "internal_recovery"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_reason_name(static_cast<factory_reset_reason_t>(99)),
            "unknown"
        ) == 0
    );

    assert(std::strcmp(factory_reset_error_name(FACTORY_RESET_OK), "ok") == 0);
    assert(
        std::strcmp(
            factory_reset_error_name(FACTORY_RESET_ERR_NOT_INITIALIZED),
            "factory_reset_not_initialized"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_error_name(FACTORY_RESET_ERR_INVALID_REASON),
            "factory_reset_invalid_reason"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_error_name(FACTORY_RESET_ERR_NO_PENDING_RESET),
            "factory_reset_no_pending_request"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_error_name(FACTORY_RESET_ERR_EXPIRED),
            "factory_reset_expired"
        ) == 0
    );
    assert(
        std::strcmp(
            factory_reset_error_name(FACTORY_RESET_ERR_STORAGE),
            "factory_reset_storage_error"
        ) == 0
    );
}

void test_shutdown_disarms_and_releases_resources() {
    prepare();
    assert(
        factory_reset_request(FACTORY_RESET_REASON_GUARDIAN_REQUEST) ==
        FACTORY_RESET_OK
    );
    descriptor()->shutdown();
    assert(!factory_reset_get_snapshot().is_ready);
    assert(!factory_reset_is_pending());
    assert(factory_reset_host_timer_delete_calls() == 1);
    assert(factory_reset_host_mutex_delete_calls() >= 1);

    // A second shutdown is a harmless no-op.
    descriptor()->shutdown();

    factory_reset_host_reset();
    assert(descriptor()->initialize() == ESP_OK);
    assert(factory_reset_get_snapshot().is_ready);
    descriptor()->shutdown();
}

}  // namespace

int main() {
    test_calls_before_init_are_refused();
    test_init_failures_are_reported_and_cleaned_up();
    test_init_is_idempotent_with_one_timer();
    test_request_validates_reasons();
    test_request_arms_the_guarded_window();
    test_re_request_replaces_reason_and_restarts_the_window();
    test_timer_start_failure_disarms_the_request();
    test_confirm_without_a_request_is_refused();
    test_confirm_erases_config_and_partition();
    test_confirm_twice_requires_a_new_request();
    test_expired_request_is_rejected_without_erasing();
    test_pending_accessor_expires_the_request();
    test_timer_callback_expires_the_request();
    test_cancel_disarms_without_erasing();
    test_partition_erase_failure_is_reported_as_storage();
    test_missing_config_store_still_erases_the_partition();
    test_uninitialised_nvs_is_tolerated();
    test_stale_nvs_partition_is_erased_and_re_initialised();
    test_reason_and_error_names_are_stable();
    test_shutdown_disarms_and_releases_resources();
    return 0;
}
