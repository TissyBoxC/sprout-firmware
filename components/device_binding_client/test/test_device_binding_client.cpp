#include "device_binding_client.h"

#include <cassert>
#include <cstring>

#include "device_binding_client_host.h"
#include "config_store.h"
#include "esp_http_client.h"
#include "transport_security.h"

namespace {

void reset_all() {
    device_binding_client_host_reset_module();
    device_binding_client_host_reset();
}

void prepare_ready() {
    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    assert(device_binding_client_host_is_ready());
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    device_binding_client_host_set_http_sequence(statuses, bodies, 1);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        device_binding_client_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "device_binding_client") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_init_requires_config_store_and_persists_platform_url() {
    reset_all();
    device_binding_client_host_set_config_ready(false);
    assert(device_binding_client_init() == ESP_ERR_INVALID_STATE);
    assert(!device_binding_client_host_is_ready());

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    assert(device_binding_client_host_is_ready());
    assert(
        strcmp(
            device_binding_client_host_stored_string("platform_url"),
            "https://api.clarkhub.cn"
        ) == 0
    );
    assert(device_binding_client_init() == ESP_OK);
}

void test_get_platform_base_url_is_bounded() {
    prepare_ready();
    char base_url[384] = {0};
    assert(
        device_binding_client_get_platform_base_url(
            base_url,
            sizeof(base_url)
        ) == ESP_OK
    );
    assert(strcmp(base_url, "https://api.clarkhub.cn") == 0);
    assert(
        device_binding_client_get_platform_base_url(nullptr, 0) ==
        ESP_ERR_INVALID_ARG
    );
    assert(
        device_binding_client_get_platform_base_url(base_url, 10) ==
        ESP_ERR_INVALID_SIZE
    );
}

void test_register_posts_identity_capabilities_and_marks_registered() {
    prepare_ready();
    set_single_response(201, "{\"data\":{\"registered\":true}}");

    assert(
        device_binding_client_register(
            "registration-token",
            "sprout-s3-n16r8",
            "1.2.3"
        ) == ESP_OK
    );
    assert(device_binding_client_host_request_count() == 1);
    assert(
        strcmp(
            device_binding_client_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/register"
        ) == 0
    );
    assert(
        device_binding_client_host_request_method(0) == HTTP_METHOD_POST
    );
    const char *body = device_binding_client_host_request_body(0);
    assert(strstr(body, "\"registration_token\":\"registration-token\"") != nullptr);
    assert(strstr(body, "\"device_id\":\"device-0001\"") != nullptr);
    assert(strstr(body, "\"hardware_model\":\"sprout-s3-n16r8\"") != nullptr);
    assert(strstr(body, "\"firmware_version\":\"1.2.3\"") != nullptr);
    assert(strstr(body, "\"public_key\":") != nullptr);
    assert(strstr(body, "\"capabilities\":[\"audio_input\",\"audio_output\",\"wifi\",\"bluetooth_audio\"]") != nullptr);
    assert(device_binding_client_is_registered());
    assert(
        strcmp(
            device_binding_client_host_stored_string("dev_registered"),
            "1"
        ) == 0
    );
}

void test_register_requires_token_and_ready_runtime() {
    reset_all();
    assert(
        device_binding_client_register("token", "model", "version") ==
        ESP_ERR_INVALID_STATE
    );

    prepare_ready();
    assert(
        device_binding_client_register(nullptr, "model", "version") ==
        ESP_ERR_INVALID_ARG
    );
    assert(
        device_binding_client_register("", "model", "version") ==
        ESP_ERR_INVALID_ARG
    );
    assert(device_binding_client_host_request_count() == 0);
}

void test_register_pending_erases_grant_only_after_success() {
    prepare_ready();
    assert(
        device_binding_client_host_stored_string("dev_reg_token")[0] == '\0'
    );
    assert(
        device_binding_client_register_pending() ==
        CONFIG_STORE_ERR_NOT_FOUND
    );
    assert(device_binding_client_host_request_count() == 0);

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_seed_string("dev_reg_token", "grant-1");
    set_single_response(500, "{}");
    assert(
        device_binding_client_register_pending() == ESP_ERR_INVALID_RESPONSE
    );
    assert(
        strcmp(
            device_binding_client_host_stored_string("dev_reg_token"),
            "grant-1"
        ) == 0
    );

    set_single_response(201, "{\"data\":{\"registered\":true}}");
    assert(device_binding_client_register_pending() == ESP_OK);
    assert(!device_binding_client_host_has_key("dev_reg_token"));
}

void test_authenticate_completes_challenge_and_stores_session() {
    prepare_ready();
    const int statuses[] = {201, 200, 200};
    const char *bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{\"data\":{\"nonce\":\"nonce-123\"}}",
        "{\"data\":{\"device_session_token\":\"session-abc\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 3);
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);

    assert(device_binding_client_authenticate() == ESP_OK);
    assert(device_binding_client_host_request_count() == 3);
    assert(
        strcmp(
            device_binding_client_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/auth/challenge"
        ) == 0
    );
    assert(
        strcmp(
            device_binding_client_host_request_body(1),
            "{\"device_id\":\"device-0001\"}"
        ) == 0
    );
    assert(
        strcmp(
            device_binding_client_host_request_url(2),
            "https://api.clarkhub.cn/api/v1/devices/auth/complete"
        ) == 0
    );
    const char *complete_body = device_binding_client_host_request_body(2);
    assert(strstr(complete_body, "\"nonce\":\"nonce-123\"") != nullptr);
    assert(strstr(complete_body, "\"signature\":") != nullptr);
    assert(
        strcmp(
            device_binding_client_host_last_signed_message(),
            "device-0001.nonce-123"
        ) == 0
    );
    assert(device_binding_client_host_last_signed_message_size() == 21u);

    char token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    assert(
        device_binding_client_copy_session_token(token, sizeof(token)) ==
        ESP_OK
    );
    assert(strcmp(token, "session-abc") == 0);
    assert(
        strcmp(
            device_binding_client_host_stored_string("device_session"),
            "session-abc"
        ) == 0
    );
}

void test_lost_registration_response_recovers_only_after_device_proof() {
    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_seed_string("dev_reg_token", "grant-1");

    // The platform consumed the grant but the original 201 response was lost.
    const int statuses[] = {
        409, 200, 200,
    };
    const char *bodies[] = {
        "{\"schema_version\":\"1.0.0\",\"request_id\":\"req-1\","
        "\"data\":null,\"error\":{\"code\":\"registration_used\","
        "\"message\":\"设备已经完成注册\",\"retryable\":false}}",
        "{\"data\":{\"nonce\":\"nonce-recovery\"}}",
        "{\"data\":{\"device_session_token\":\"session-recovered\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 3);
    // register_pending drives the challenge proof itself and only completes
    // once the platform verifies this device's private key.
    assert(device_binding_client_register_pending() == ESP_OK);
    assert(device_binding_client_host_request_count() == 3);
    assert(
        strcmp(
            device_binding_client_host_last_signed_message(),
            "device-0001.nonce-recovery"
        ) == 0
    );
    assert(device_binding_client_is_registered());
    assert(!device_binding_client_host_has_key("dev_reg_token"));
    assert(
        strcmp(
            device_binding_client_host_stored_string("dev_registered"),
            "1"
        ) == 0
    );
    assert(
        strcmp(
            device_binding_client_host_stored_string("device_session"),
            "session-recovered"
        ) == 0
    );
}

void test_registration_conflict_does_not_hide_real_identity_mismatch() {
    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_seed_string("dev_reg_token", "grant-2");

    const int statuses[] = {409, 401};
    const char *bodies[] = {
        "{\"error\":{\"code\":\"registration_used\"}}",
        "{\"error\":{\"code\":\"device_auth_failed\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 2);
    assert(
        device_binding_client_register_pending() == ESP_ERR_INVALID_RESPONSE
    );
    assert(!device_binding_client_is_registered());
    assert(
        strcmp(
            device_binding_client_host_stored_string("dev_reg_token"),
            "grant-2"
        ) == 0
    );
    assert(!device_binding_client_host_has_key("dev_registered"));
}

void test_other_conflicts_and_malformed_409_remain_failures() {
    prepare_ready();
    set_single_response(409, "{\"error\":{\"code\":\"token_used\"}}");
    assert(
        device_binding_client_register("token", "model", "1.0") ==
        ESP_ERR_INVALID_RESPONSE
    );
    assert(!device_binding_client_is_registered());

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    set_single_response(409, "{\"error\":{\"message\":\"missing code\"}}");
    assert(
        device_binding_client_register("token", "model", "1.0") ==
        ESP_ERR_INVALID_RESPONSE
    );
    assert(!device_binding_client_is_registered());

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    set_single_response(409, "registration_used");
    assert(
        device_binding_client_register("token", "model", "1.0") ==
        ESP_ERR_INVALID_RESPONSE
    );
    assert(!device_binding_client_is_registered());
}

void test_direct_registration_reports_consumed_grant_for_caller_recovery() {
    prepare_ready();
    const int statuses[] = {409, 200, 200};
    const char *bodies[] = {
        "{\"error\":{\"code\":\"registration_used\"}}",
        "{\"data\":{\"nonce\":\"nonce-direct\"}}",
        "{\"data\":{\"device_session_token\":\"session-direct\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 3);
    assert(
        device_binding_client_register("token", "model", "1.0") ==
        DEVICE_BINDING_REGISTRATION_ALREADY_COMPLETED
    );
    assert(!device_binding_client_is_registered());

    assert(device_binding_client_authenticate() == ESP_OK);
    assert(device_binding_client_is_registered());
}

void test_conflict_recovery_keeps_grant_when_proof_storage_fails() {
    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_seed_string("dev_reg_token", "grant-3");
    const int statuses[] = {
        409, 200, 200,
    };
    const char *bodies[] = {
        "{\"error\":{\"code\":\"registration_used\"}}",
        "{\"data\":{\"nonce\":\"nonce-storage-failure\"}}",
        "{\"data\":{\"device_session_token\":\"session-abc\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 3);
    device_binding_client_host_set_store_set_string_result(
        ESP_ERR_INVALID_STATE
    );
    assert(device_binding_client_register_pending() != ESP_OK);
    device_binding_client_host_set_store_set_string_result(ESP_OK);
    assert(!device_binding_client_is_registered());
    assert(
        strcmp(
            device_binding_client_host_stored_string("dev_reg_token"),
            "grant-3"
        ) == 0
    );
    assert(!device_binding_client_host_has_key("dev_registered"));
}

void test_conflict_recovery_commits_after_grant_erase_failure() {
    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_seed_string("dev_reg_token", "grant-4");
    const int statuses[] = {409, 200, 200};
    const char *bodies[] = {
        "{\"error\":{\"code\":\"registration_used\"}}",
        "{\"data\":{\"nonce\":\"nonce-erase-failure\"}}",
        "{\"data\":{\"device_session_token\":\"session-erase\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 3);
    device_binding_client_host_set_store_erase_result(ESP_ERR_INVALID_STATE);
    assert(
        device_binding_client_register_pending() == ESP_ERR_INVALID_STATE
    );
    assert(!device_binding_client_is_registered());
    assert(
        strcmp(
            device_binding_client_host_stored_string("dev_reg_token"),
            "grant-4"
        ) == 0
    );

    // The platform proof succeeded, so retrying registration is enough to
    // finish the local commit without another challenge round trip.
    device_binding_client_host_set_store_erase_result(ESP_OK);
    set_single_response(409, "{\"error\":{\"code\":\"registration_used\"}}");
    assert(device_binding_client_register_pending() == ESP_OK);
    assert(device_binding_client_is_registered());
    assert(!device_binding_client_host_has_key("dev_reg_token"));
}

void test_authenticate_requires_registration_and_valid_challenge() {
    prepare_ready();
    assert(device_binding_client_authenticate() == ESP_ERR_INVALID_STATE);

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    set_single_response(201, "{\"data\":{\"registered\":true}}");
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(device_binding_client_is_registered());
    set_single_response(200, "{\"data\":{}}");
    assert(
        device_binding_client_authenticate() == ESP_ERR_INVALID_RESPONSE
    );
}

void test_clear_session_token_removes_persisted_and_memory_state() {
    prepare_ready();
    const int statuses[] = {201, 200, 200};
    const char *bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{\"data\":{\"nonce\":\"nonce-123\"}}",
        "{\"data\":{\"device_session_token\":\"session-abc\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 3);
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(device_binding_client_authenticate() == ESP_OK);
    assert(device_binding_client_clear_session_token() == ESP_OK);
    char cleared_session[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {};
    assert(
        device_binding_client_copy_session_token(
            cleared_session,
            DEVICE_BINDING_SESSION_TOKEN_SIZE
        ) == ESP_ERR_INVALID_STATE
    );
    assert(!device_binding_client_host_has_key("device_session"));
}

void test_create_provisioning_token_uses_session_and_bounded_response() {
    prepare_ready();
    const int statuses[] = {201, 200, 200, 200};
    const char *bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{\"data\":{\"nonce\":\"nonce-123\"}}",
        "{\"data\":{\"device_session_token\":\"session-abc\"}}",
        "{\"data\":{\"token\":\"binding-token\"}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 4);
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(device_binding_client_authenticate() == ESP_OK);

    char token[DEVICE_BINDING_TOKEN_SIZE] = {0};
    assert(
        device_binding_client_create_provisioning_token(
            token,
            sizeof(token)
        ) == ESP_OK
    );
    assert(strcmp(token, "binding-token") == 0);
    assert(
        strcmp(
            device_binding_client_host_request_authorization(3),
            "Bearer session-abc"
        ) == 0
    );
    assert(
        strcmp(
            device_binding_client_host_request_url(3),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "provisioning-token"
        ) == 0
    );
}

void test_check_binding_persists_transitions_and_callbacks() {
    prepare_ready();
    const int statuses[] = {201, 200, 200, 200};
    const char *bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{\"data\":{\"nonce\":\"nonce-123\"}}",
        "{\"data\":{\"device_session_token\":\"session-abc\"}}",
        "{\"data\":{\"is_bound\":true}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 4);
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(device_binding_client_authenticate() == ESP_OK);
    struct callback_state {
        int calls;
        bool value;
    };
    callback_state state = {};
    callback_state completed = {};

    assert(
        device_binding_client_set_state_callback(
            [](bool is_bound, void *context) {
                callback_state *state = (callback_state *)context;
                state->calls += 1;
                state->value = is_bound;
            },
            &state
        ) == ESP_OK
    );
    assert(
        device_binding_client_set_completion_callback(
            [](bool is_bound, void *context) {
                callback_state *state = (callback_state *)context;
                state->calls += 1;
                state->value = is_bound;
            },
            &completed
        ) == ESP_OK
    );

    bool is_bound = false;
    assert(device_binding_client_check_binding(&is_bound) == ESP_OK);
    assert(is_bound);
    assert(device_binding_client_is_bound());
    assert(state.calls == 1);
    assert(completed.calls == 1);
    assert(state.value);
    assert(completed.value);
    assert(
        strcmp(
            device_binding_client_host_stored_string("device_bound"),
            "1"
        ) == 0
    );

    const int unbind_statuses[] = {
        200, 200, 200, 200, 200, 200
    };
    const char *unbind_bodies[] = {
        "{\"data\":{\"is_bound\":true}}",
        "{\"data\":{\"is_bound\":true}}",
        "{\"data\":{\"is_bound\":true}}",
        "{\"data\":{\"is_bound\":true}}",
        "{\"data\":{\"is_bound\":false}}",
        "{\"data\":{\"is_bound\":false}}",
    };
    device_binding_client_host_set_http_sequence(
        unbind_statuses,
        unbind_bodies,
        6
    );
    assert(device_binding_client_check_binding(&is_bound) == ESP_OK);
    assert(!is_bound);
    assert(!device_binding_client_is_bound());
    assert(state.calls == 2);
    assert(!state.value);
    assert(!device_binding_client_host_has_key("device_bound"));
}

void test_check_binding_rejects_missing_session_and_malformed_response() {
    prepare_ready();
    bool is_bound = false;
    assert(
        device_binding_client_check_binding(&is_bound) ==
        ESP_ERR_INVALID_STATE
    );
    assert(
        device_binding_client_check_binding(nullptr) == ESP_ERR_INVALID_ARG
    );

    const int statuses[] = {201, 200, 200, 200};
    const char *bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{\"data\":{\"nonce\":\"nonce-123\"}}",
        "{\"data\":{\"device_session_token\":\"session-abc\"}}",
        "{\"data\":{}}",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 4);
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(device_binding_client_authenticate() == ESP_OK);
    assert(
        device_binding_client_check_binding(&is_bound) ==
        ESP_ERR_INVALID_RESPONSE
    );
}

void test_refresh_state_restores_all_persisted_flags() {
    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    assert(
        device_binding_client_host_stored_string("device_bound")[0] == '\0'
    );
    assert(!device_binding_client_is_bound());
    assert(!device_binding_client_is_registered());
    char missing_session[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {};
    assert(
        device_binding_client_copy_session_token(
            missing_session,
            DEVICE_BINDING_SESSION_TOKEN_SIZE
        ) == ESP_ERR_INVALID_STATE
    );
}

void test_http_and_transport_failures_are_bounded() {
    prepare_ready();
    device_binding_client_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(
        device_binding_client_register("token", "model", "1.0") ==
        ESP_ERR_INVALID_ARG
    );
    assert(device_binding_client_host_request_count() == 0);

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_set_client_init_fails(true);
    assert(
        device_binding_client_register("token", "model", "1.0") == ESP_FAIL
    );

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    device_binding_client_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(
        device_binding_client_register("token", "model", "1.0") ==
        ESP_ERR_TIMEOUT
    );
}

void test_invalid_json_and_missing_data_are_rejected() {
    prepare_ready();
    const int statuses[] = {201, 200};
    const char *bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{not-json",
    };
    device_binding_client_host_set_http_sequence(statuses, bodies, 2);
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(
        device_binding_client_authenticate() == ESP_ERR_INVALID_RESPONSE
    );
    assert(device_binding_client_host_request_count() == 2);

    reset_all();
    assert(device_binding_client_init() == ESP_OK);
    const int missing_statuses[] = {201, 200};
    const char *missing_bodies[] = {
        "{\"data\":{\"registered\":true}}",
        "{\"data\":[]}",
    };
    device_binding_client_host_set_http_sequence(
        missing_statuses,
        missing_bodies,
        2
    );
    assert(device_binding_client_register("token", "model", "1.0") == ESP_OK);
    assert(
        device_binding_client_authenticate() ==
        ESP_ERR_INVALID_RESPONSE
    );
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_init_requires_config_store_and_persists_platform_url();
    test_get_platform_base_url_is_bounded();
    test_register_posts_identity_capabilities_and_marks_registered();
    test_register_requires_token_and_ready_runtime();
    test_register_pending_erases_grant_only_after_success();
    test_authenticate_completes_challenge_and_stores_session();
    test_lost_registration_response_recovers_only_after_device_proof();
    test_registration_conflict_does_not_hide_real_identity_mismatch();
    test_other_conflicts_and_malformed_409_remain_failures();
    test_direct_registration_reports_consumed_grant_for_caller_recovery();
    test_conflict_recovery_keeps_grant_when_proof_storage_fails();
    test_conflict_recovery_commits_after_grant_erase_failure();
    test_authenticate_requires_registration_and_valid_challenge();
    test_clear_session_token_removes_persisted_and_memory_state();
    test_create_provisioning_token_uses_session_and_bounded_response();
    test_check_binding_persists_transitions_and_callbacks();
    test_check_binding_rejects_missing_session_and_malformed_response();
    test_refresh_state_restores_all_persisted_flags();
    test_http_and_transport_failures_are_bounded();
    test_invalid_json_and_missing_data_are_rejected();
    return 0;
}
