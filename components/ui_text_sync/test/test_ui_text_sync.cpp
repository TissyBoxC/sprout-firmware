#include "ui_text_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "network_manager.h"
#include "transport_security.h"
#include "ui_text_sync_host.h"

namespace {

const char *empty_page() {
    return "{\"data\":{\"revision\":0,\"has_more\":false,"
           "\"packages\":[],\"withdrawn_packages\":[]}}";
}

const char *published_page() {
    return "{\"data\":{\"revision\":12,\"has_more\":false,"
           "\"packages\":[{\"package_id\":\"home_welcome\","
           "\"language\":\"zh-CN\",\"package_version\":3,"
           "\"revision\":12}],\"withdrawn_packages\":[]}}";
}

const char *published_package() {
    return "{\"data\":{\"package\":{\"package_id\":\"home_welcome\","
           "\"package_version\":3,\"language\":\"zh-CN\"},"
           "\"entries\":[{\"ui_text_key\":\"home.greeting\","
           "\"value\":\"你好\",\"max_length\":16,"
           "\"variables\":[]}]}}";
}

const char *withdrawal_page() {
    return "{\"data\":{\"revision\":14,\"has_more\":false,"
           "\"packages\":[],\"withdrawn_packages\":["
           "{\"package_id\":\"home_welcome\",\"language\":\"zh-CN\","
           "\"revision\":14}]}}";
}

void reset_all() {
    ui_text_sync_host_reset();
    ui_text_sync_host_reset_store();
}

void set_sequence(const int *statuses, const char *const *bodies, size_t count) {
    const int default_statuses[] = {200};
    ui_text_sync_host_set_http_sequence(
        statuses != nullptr ? statuses : default_statuses,
        bodies,
        count
    );
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        ui_text_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "ui_text_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_network_and_identity() {
    reset_all();
    ui_text_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(ui_text_sync_now() == ESP_ERR_INVALID_STATE);
    assert(ui_text_sync_host_request_count() == 0);

    reset_all();
    ui_text_sync_host_set_time_synchronized(false);
    assert(ui_text_sync_now() == ESP_ERR_INVALID_STATE);
    assert(ui_text_sync_host_request_count() == 0);

    reset_all();
    ui_text_sync_host_set_registered(false);
    assert(ui_text_sync_now() == ESP_ERR_INVALID_STATE);
    assert(ui_text_sync_host_request_count() == 0);
}

void test_empty_page_succeeds_and_persists_cursor() {
    reset_all();
    const char *body = empty_page();
    set_sequence(nullptr, &body, 1);
    assert(ui_text_sync_now() == ESP_OK);
    assert(ui_text_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            ui_text_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "ui-text/packages?since_revision=0&limit=32"
        ) == 0
    );
    assert(
        std::strcmp(
            ui_text_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(ui_text_sync_host_transport_security_calls() == 1);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);
}

void test_published_package_is_applied_and_cursor_advances() {
    reset_all();
    const int statuses[] = {200, 200};
    const char *bodies[] = {published_page(), published_package()};
    set_sequence(statuses, bodies, 2);
    assert(ui_text_sync_now() == ESP_OK);
    assert(ui_text_sync_host_request_count() == 2);
    assert(
        std::strcmp(
            ui_text_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "ui-text/packages/home_welcome/zh-CN/device_firmware"
        ) == 0
    );
    assert(ui_text_sync_host_apply_calls() == 1);
    assert(
        std::strcmp(
            ui_text_sync_host_last_applied_package(),
            "home_welcome"
        ) == 0
    );
    assert(std::strcmp(ui_text_sync_host_cursor(), "12") == 0);
}

void test_package_apply_failure_does_not_advance_cursor() {
    reset_all();
    const int statuses[] = {200, 200};
    const char *bodies[] = {published_page(), published_package()};
    set_sequence(statuses, bodies, 2);
    ui_text_sync_host_set_apply_result(ESP_ERR_NO_MEM);
    assert(ui_text_sync_now() == ESP_ERR_NO_MEM);
    assert(ui_text_sync_host_apply_calls() == 1);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);
}

void test_cursor_persistence_failure_is_propagated() {
    reset_all();
    const char *body = empty_page();
    set_sequence(nullptr, &body, 1);
    ui_text_sync_host_set_store_string_result(ESP_FAIL);
    assert(ui_text_sync_now() == ESP_FAIL);
    assert(ui_text_sync_host_request_count() == 1);
    assert(ui_text_sync_host_set_string_calls() == 1);
}

void test_withdrawal_removes_active_package_and_advances_cursor() {
    reset_all();
    ui_text_sync_host_set_active_package("home_welcome", "zh-CN", 3);
    const char *body = withdrawal_page();
    set_sequence(nullptr, &body, 1);
    assert(ui_text_sync_now() == ESP_OK);
    assert(ui_text_sync_host_withdraw_calls() == 1);
    assert(
        std::strcmp(
            ui_text_sync_host_last_withdrawn_package(),
            "home_welcome"
        ) == 0
    );
    assert(std::strcmp(ui_text_sync_host_cursor(), "14") == 0);
}

void test_withdrawal_failure_does_not_advance_cursor() {
    reset_all();
    ui_text_sync_host_set_active_package("home_welcome", "zh-CN", 3);
    const char *body = withdrawal_page();
    set_sequence(nullptr, &body, 1);
    ui_text_sync_host_set_withdraw_result(ESP_FAIL);
    assert(ui_text_sync_now() == ESP_FAIL);
    assert(ui_text_sync_host_withdraw_calls() == 1);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);
}

void test_transport_and_auth_errors_are_distinct() {
    reset_all();
    const char *body = empty_page();
    set_sequence(nullptr, &body, 1);
    ui_text_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(ui_text_sync_now() == ESP_ERR_INVALID_STATE);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);

    reset_all();
    const int status[] = {401};
    set_sequence(status, &body, 1);
    assert(ui_text_sync_now() == ESP_ERR_NOT_FOUND);
    assert(ui_text_sync_host_clear_session_calls() == 1);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);

    reset_all();
    const int forbidden[] = {403};
    set_sequence(forbidden, &body, 1);
    assert(ui_text_sync_now() == ESP_ERR_NOT_FOUND);
    assert(ui_text_sync_host_clear_session_calls() == 1);
}

void test_server_and_validation_errors_do_not_persist_cursor() {
    reset_all();
    const int unavailable[] = {503};
    const char *server_body[] = {"{\"error\":\"unavailable\"}"};
    set_sequence(unavailable, server_body, 1);
    assert(ui_text_sync_now() == ESP_FAIL);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);

    reset_all();
    const char *invalid =
        "{\"data\":{\"revision\":9,\"has_more\":false}}";
    set_sequence(nullptr, &invalid, 1);
    assert(ui_text_sync_now() == ESP_ERR_INVALID_ARG);
    assert(std::strcmp(ui_text_sync_host_cursor(), "0") == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_network_and_identity();
    test_empty_page_succeeds_and_persists_cursor();
    test_published_package_is_applied_and_cursor_advances();
    test_package_apply_failure_does_not_advance_cursor();
    test_cursor_persistence_failure_is_propagated();
    test_withdrawal_removes_active_package_and_advances_cursor();
    test_withdrawal_failure_does_not_advance_cursor();
    test_transport_and_auth_errors_are_distinct();
    test_server_and_validation_errors_do_not_persist_cursor();
    return 0;
}
