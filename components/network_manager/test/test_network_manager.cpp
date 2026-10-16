#include "network_manager.h"

#include "config_store.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include <cassert>
#include <cstring>

void network_manager_host_reset(void);
void network_manager_host_set_create_mutex_fails(bool fails);
void network_manager_host_set_config_store_ready(bool ready);
void network_manager_host_set_netif_init_result(esp_err_t result);
void network_manager_host_set_event_loop_result(esp_err_t result);
void network_manager_host_set_create_netif_null(bool returns_null);
void network_manager_host_set_wifi_init_result(esp_err_t result);
void network_manager_host_set_register_result(esp_err_t result);
void network_manager_host_set_set_mode_result(esp_err_t result);
void network_manager_host_set_wifi_start_result(esp_err_t result);
void network_manager_host_set_connect_result(esp_err_t result);
void network_manager_host_set_disconnect_result(esp_err_t result);
void network_manager_host_set_set_config_result(esp_err_t result);
void network_manager_host_set_get_ap_info_result(esp_err_t result);
void network_manager_host_set_store_set_result(esp_err_t result);
void network_manager_host_set_store_get_result(esp_err_t result);
void network_manager_host_set_ap_ssid(const char *ssid);
void network_manager_host_emit_wifi_event(int32_t event_id);
void network_manager_host_emit_ip_event(void);
int network_manager_host_netif_init_calls(void);
int network_manager_host_create_netif_calls(void);
int network_manager_host_wifi_init_calls(void);
int network_manager_host_wifi_start_calls(void);
int network_manager_host_connect_calls(void);
int network_manager_host_disconnect_calls(void);
int network_manager_host_set_config_calls(void);
int network_manager_host_store_set_calls(void);
int network_manager_host_store_get_calls(void);
wifi_config_t network_manager_host_last_wifi_config(void);
wifi_mode_t network_manager_host_last_mode(void);
wifi_storage_t network_manager_host_last_storage(void);

namespace {

struct callback_record_t {
    network_manager_state_t state;
    void *context;
};

callback_record_t g_records[16];
int g_record_count;

void record_state(network_manager_state_t state, void *context) {
    if (g_record_count < 16) {
        g_records[g_record_count].state = state;
        g_records[g_record_count].context = context;
    }
    ++g_record_count;
}

void reset_records() {
    g_record_count = 0;
    std::memset(g_records, 0, sizeof(g_records));
}

// Distinct observers for the bounded-table case. Identical captureless lambdas
// are merged into one function pointer, which the module would deduplicate.
void observer_a(network_manager_state_t, void *) {}
void observer_b(network_manager_state_t, void *) {}
void observer_c(network_manager_state_t, void *) {}
void observer_d(network_manager_state_t, void *) {}
void observer_e(network_manager_state_t, void *) {}

// network_manager has no shutdown, so the very first successful init happens
// once per process. These assertions run before it.
void test_calls_before_init_are_refused() {
    network_manager_host_reset();
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_IDLE);
    assert(network_manager_connect("home", "secret") == ESP_ERR_INVALID_STATE);
    assert(network_manager_disconnect() == ESP_ERR_INVALID_STATE);
    assert(network_manager_copy_ssid(nullptr, 0) == ESP_ERR_INVALID_ARG);
}

void test_init_requires_a_ready_config_store() {
    network_manager_host_reset();
    network_manager_host_set_config_store_ready(false);
    assert(network_manager_init() == ESP_ERR_INVALID_STATE);
}

void test_init_failures_are_propagated() {
    network_manager_host_reset();
    network_manager_host_set_create_mutex_fails(true);
    assert(network_manager_init() == ESP_ERR_NO_MEM);

    network_manager_host_reset();
    network_manager_host_set_netif_init_result(ESP_FAIL);
    assert(network_manager_init() == ESP_FAIL);

    network_manager_host_reset();
    network_manager_host_set_create_netif_null(true);
    assert(network_manager_init() == ESP_FAIL);

    network_manager_host_reset();
    network_manager_host_set_wifi_init_result(ESP_ERR_NO_MEM);
    assert(network_manager_init() == ESP_ERR_NO_MEM);

    network_manager_host_reset();
    network_manager_host_set_register_result(ESP_ERR_INVALID_STATE);
    assert(network_manager_init() == ESP_ERR_INVALID_STATE);

    network_manager_host_reset();
    network_manager_host_set_set_mode_result(ESP_ERR_INVALID_ARG);
    assert(network_manager_init() == ESP_ERR_INVALID_ARG);

    network_manager_host_reset();
    network_manager_host_set_wifi_start_result(ESP_ERR_TIMEOUT);
    assert(network_manager_init() == ESP_ERR_TIMEOUT);
}

void test_successful_init_configures_the_station() {
    network_manager_host_reset();
    assert(network_manager_init() == ESP_OK);
    // Earlier failing inits may already have created the station netif; the
    // module deliberately reuses it instead of allocating a second one.
    assert(network_manager_host_create_netif_calls() <= 1);
    assert(network_manager_host_wifi_init_calls() == 1);
    assert(network_manager_host_wifi_start_calls() == 1);
    assert(network_manager_host_last_mode() == WIFI_MODE_STA);
    // Credentials live in RAM so a factory reset cannot be undone by a stale
    // flash copy.
    assert(network_manager_host_last_storage() == WIFI_STORAGE_RAM);
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_IDLE);

    // Idempotent: a second init must not reconfigure the driver.
    assert(network_manager_init() == ESP_OK);
    assert(network_manager_host_wifi_init_calls() == 1);
    assert(network_manager_host_wifi_start_calls() == 1);
}

void test_store_credentials_validates_input() {
    assert(network_manager_store_credentials(nullptr, "x") == ESP_ERR_INVALID_ARG);
    assert(network_manager_store_credentials("", "x") == ESP_ERR_INVALID_ARG);
    assert(network_manager_store_credentials("ok", "x") == ESP_OK);

    // Exactly at the limit is accepted, one over is not.
    char ssid[CONFIG_STORE_SSID_SIZE + 2];
    std::memset(ssid, 'a', sizeof(ssid));
    ssid[CONFIG_STORE_SSID_SIZE] = '\0';
    assert(network_manager_store_credentials(ssid, nullptr) == ESP_OK);
    ssid[CONFIG_STORE_SSID_SIZE] = 'a';
    ssid[CONFIG_STORE_SSID_SIZE + 1] = '\0';
    assert(network_manager_store_credentials(ssid, nullptr) == ESP_ERR_INVALID_ARG);

    char password[CONFIG_STORE_PASSWORD_SIZE + 2];
    std::memset(password, 'b', sizeof(password));
    password[CONFIG_STORE_PASSWORD_SIZE] = '\0';
    assert(network_manager_store_credentials("ok", password) == ESP_OK);
    password[CONFIG_STORE_PASSWORD_SIZE] = 'b';
    password[CONFIG_STORE_PASSWORD_SIZE + 1] = '\0';
    assert(network_manager_store_credentials("ok", password) == ESP_ERR_INVALID_ARG);
}

void test_connect_persists_credentials_before_joining() {
    network_manager_host_reset();
    reset_records();
    assert(
        network_manager_add_state_callback(record_state, &g_record_count) ==
        ESP_OK
    );

    assert(network_manager_connect("home", "secret") == ESP_OK);
    assert(network_manager_host_store_set_calls() == 2);
    assert(network_manager_host_disconnect_calls() == 1);
    assert(network_manager_host_set_config_calls() == 1);
    assert(network_manager_host_connect_calls() == 1);

    const wifi_config_t config = network_manager_host_last_wifi_config();
    assert(std::strcmp(reinterpret_cast<const char *>(config.sta.ssid), "home") == 0);
    assert(
        std::strcmp(
            reinterpret_cast<const char *>(config.sta.password),
            "secret"
        ) == 0
    );
    assert(config.sta.threshold.authmode == WIFI_AUTH_OPEN);
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTING);
    // The transition was published exactly once to the observer.
    assert(g_record_count == 1);
    assert(g_records[0].state == NETWORK_MANAGER_STATE_CONNECTING);
    assert(g_records[0].context == &g_record_count);

    assert(network_manager_remove_state_callback(record_state, &g_record_count) == ESP_OK);
}

void test_connect_rejects_bad_input_and_storage_failure() {
    assert(network_manager_connect(nullptr, nullptr) == ESP_ERR_INVALID_ARG);
    assert(network_manager_connect("", nullptr) == ESP_ERR_INVALID_ARG);

    network_manager_host_set_store_set_result(ESP_FAIL);
    const int connects_before = network_manager_host_connect_calls();
    assert(network_manager_connect("home", nullptr) == ESP_FAIL);
    // Nothing is attempted when persisting the credentials fails.
    assert(network_manager_host_connect_calls() == connects_before);
    network_manager_host_set_store_set_result(ESP_OK);
}

void test_connect_stops_when_bringing_up_the_interface_fails() {
    network_manager_host_reset();
    network_manager_host_set_set_config_result(ESP_ERR_INVALID_STATE);
    const int connects_before = network_manager_host_connect_calls();
    assert(network_manager_connect("home", nullptr) == ESP_ERR_INVALID_STATE);
    assert(network_manager_host_connect_calls() == connects_before);

    network_manager_host_set_set_config_result(ESP_OK);
    network_manager_host_set_connect_result(ESP_FAIL);
    assert(network_manager_connect("home", nullptr) == ESP_FAIL);
}

void test_got_ip_and_disconnect_drive_the_state_machine() {
    network_manager_host_reset();
    reset_records();
    assert(
        network_manager_add_state_callback(record_state, nullptr) == ESP_OK
    );

    assert(network_manager_connect("home", nullptr) == ESP_OK);
    network_manager_host_emit_ip_event();
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED);
    const int records_after_connect = g_record_count;
    assert(records_after_connect >= 1);
    assert(g_records[records_after_connect - 1].state == NETWORK_MANAGER_STATE_CONNECTED);

    // A disconnect returns to idle, then immediately retries the connection.
    const int connects_before = network_manager_host_connect_calls();
    network_manager_host_emit_wifi_event(WIFI_EVENT_STA_DISCONNECTED);
    assert(network_manager_host_connect_calls() == connects_before + 1);
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTING);

    // The handler publishes IDLE first, then CONNECTING once the retry is
    // accepted, so the observer sees both transitions.
    assert(g_record_count == records_after_connect + 2);
    assert(
        g_records[records_after_connect].state ==
        NETWORK_MANAGER_STATE_IDLE
    );
    assert(
        g_records[records_after_connect + 1].state ==
        NETWORK_MANAGER_STATE_CONNECTING
    );

    assert(network_manager_remove_state_callback(record_state, nullptr) == ESP_OK);
}

void test_disconnect_failure_keeps_the_previous_state() {
    network_manager_host_set_disconnect_result(ESP_FAIL);
    network_manager_host_emit_ip_event();
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED);
    assert(network_manager_disconnect() == ESP_FAIL);
    // A failed disconnect must not claim the station is idle.
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED);

    network_manager_host_set_disconnect_result(ESP_OK);
    assert(network_manager_disconnect() == ESP_OK);
    assert(network_manager_get_state() == NETWORK_MANAGER_STATE_IDLE);
}

void test_copy_ssid_reports_state_and_size() {
    char output[WIFI_SSID_MAX_LEN + 1];
    network_manager_host_set_ap_ssid("home-wifi");
    assert(network_manager_copy_ssid(output, sizeof(output)) == ESP_OK);
    assert(std::strcmp(output, "home-wifi") == 0);

    // A buffer that cannot hold the terminator is a size error.
    assert(network_manager_copy_ssid(output, 5) == ESP_ERR_INVALID_SIZE);
    assert(network_manager_copy_ssid(nullptr, sizeof(output)) == ESP_ERR_INVALID_ARG);
    assert(network_manager_copy_ssid(output, 0) == ESP_ERR_INVALID_ARG);

    network_manager_host_set_get_ap_info_result(ESP_ERR_WIFI_NOT_CONNECT);
    assert(
        network_manager_copy_ssid(output, sizeof(output)) ==
        ESP_ERR_WIFI_NOT_CONNECT
    );
    network_manager_host_set_get_ap_info_result(ESP_OK);
}

void test_callback_table_rejects_duplicates_and_overflows() {
    reset_records();
    assert(network_manager_add_state_callback(nullptr, nullptr) == ESP_ERR_INVALID_ARG);
    assert(network_manager_remove_state_callback(record_state, nullptr) == ESP_ERR_NOT_FOUND);

    // set_state_callback installs exactly one observer and replaces the table.
    assert(network_manager_set_state_callback(record_state, nullptr) == ESP_OK);
    assert(network_manager_add_state_callback(record_state, nullptr) == ESP_OK);
    // Same callback with a different context is a distinct observer.
    assert(
        network_manager_add_state_callback(record_state, &g_record_count) ==
        ESP_OK
    );
    // Fill the remaining slots, then confirm the bounded table refuses.
    int markers[8] = {0};
    assert(
        network_manager_add_state_callback(observer_a, &markers[0]) == ESP_OK
    );
    assert(
        network_manager_add_state_callback(observer_b, &markers[1]) == ESP_OK
    );
    assert(
        network_manager_add_state_callback(observer_c, &markers[2]) == ESP_OK
    );
    assert(
        network_manager_add_state_callback(observer_d, &markers[3]) == ESP_OK
    );
    assert(
        network_manager_add_state_callback(observer_e, &markers[6]) ==
        ESP_ERR_NO_MEM
    );

    // Removing an observer compacts the table and frees a slot.
    assert(network_manager_remove_state_callback(record_state, nullptr) == ESP_OK);
    assert(
        network_manager_add_state_callback(observer_e, &markers[4]) == ESP_OK
    );

    // Clearing the single observer also clears the table.
    assert(network_manager_set_state_callback(nullptr, nullptr) == ESP_OK);
    assert(
        network_manager_add_state_callback(observer_a, &markers[5]) == ESP_OK
    );
    assert(network_manager_remove_state_callback(nullptr, nullptr) == ESP_ERR_NOT_FOUND);
}

void test_reconnect_stored_requires_credentials() {
    network_manager_host_reset();
    network_manager_host_set_store_get_result(CONFIG_STORE_ERR_NOT_FOUND);
    assert(
        network_manager_reconnect_stored() == CONFIG_STORE_ERR_NOT_FOUND
    );
    assert(network_manager_host_connect_calls() == 0);

    network_manager_host_set_store_get_result(ESP_FAIL);
    assert(network_manager_reconnect_stored() == ESP_FAIL);

    network_manager_host_set_store_get_result(ESP_OK);
    // Seed a stored network so the reconnect has usable credentials; the
    // store read is driven by config_store_get_string.
    assert(network_manager_store_credentials("home", "secret") == ESP_OK);
    const int gets_before = network_manager_host_store_get_calls();
    const int connects_before = network_manager_host_connect_calls();
    assert(network_manager_reconnect_stored() == ESP_OK);
    assert(network_manager_host_store_get_calls() == gets_before + 2);
    assert(network_manager_host_connect_calls() == connects_before + 1);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        network_manager_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "network_manager") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_calls_before_init_are_refused();
    test_init_requires_a_ready_config_store();
    test_init_failures_are_propagated();
    test_successful_init_configures_the_station();
    test_store_credentials_validates_input();
    test_connect_persists_credentials_before_joining();
    test_connect_rejects_bad_input_and_storage_failure();
    test_connect_stops_when_bringing_up_the_interface_fails();
    test_got_ip_and_disconnect_drive_the_state_machine();
    test_disconnect_failure_keeps_the_previous_state();
    test_copy_ssid_reports_state_and_size();
    test_callback_table_rejects_duplicates_and_overflows();
    test_reconnect_stored_requires_credentials();
    test_module_descriptor_is_stable();
    return 0;
}
