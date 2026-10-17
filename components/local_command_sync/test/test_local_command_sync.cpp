#include "local_command_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "local_command.h"
#include "local_command_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    local_command_sync_host_reset_module();
    local_command_sync_host_reset();
    local_command_sync_host_reset_store();
    local_command_test_reset_ready();
    assert(local_command_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    local_command_sync_host_set_http_sequence(statuses, bodies, 1);
}

local_command_t make_command(
    const char *command_id,
    const char *phrase,
    local_command_action_t action,
    const char *argument,
    bool enabled
) {
    local_command_t command = {};
    strncpy(command.command_id, command_id, LOCAL_COMMAND_ID_SIZE - 1);
    strncpy(command.phrase, phrase, LOCAL_COMMAND_PHRASE_SIZE - 1);
    command.action = action;
    if (argument != nullptr) {
        strncpy(
            command.argument,
            argument,
            LOCAL_COMMAND_ARGUMENT_SIZE - 1
        );
    }
    command.revision = 1u;
    command.enabled = enabled;
    return command;
}

void seed_command(
    const char *command_id,
    const char *phrase,
    local_command_action_t action,
    const char *argument
) {
    const local_command_t command = make_command(
        command_id,
        phrase,
        action,
        argument,
        true
    );
    assert(local_command_replace_all(&command, 1) == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        local_command_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "local_command_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_store() {
    local_command_sync_host_reset();
    local_command_sync_host_reset_module();
    local_command_sync_host_reset_store();
    local_command_test_reset_ready();
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    local_command_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);

    local_command_sync_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    local_command_sync_host_set_time_synchronized(false);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);

    local_command_sync_host_set_time_synchronized(true);
    local_command_sync_host_set_registered(false);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    local_command_sync_host_set_copy_session_result(ESP_ERR_INVALID_SIZE);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_sync_host_request_count() == 0);

    reset_all();
    local_command_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_sync_host_request_count() == 0);

    reset_all();
    local_command_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_sync_host_request_count() == 0);

    reset_all();
    local_command_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_sync_host_request_count() == 0);
}

void test_successful_fetch_replaces_command_set() {
    reset_all();
    seed_command(
        "cmd_local",
        "本地口令",
        LOCAL_COMMAND_ACTION_STOP,
        nullptr
    );
    set_single_response(
        200,
        "{\"data\":{\"revision\":9,\"commands\":["
        "{\"command_id\":\"cmd_play\",\"phrase\":\"播放睡前故事\","
        "\"action\":\"play_package\",\"argument\":\"content_story_001\","
        "\"enabled\":true},"
        "{\"command_id\":\"cmd_stop\",\"phrase\":\"停止播放\","
        "\"action\":\"stop\"}]}}"
    );

    assert(local_command_sync_now() == ESP_OK);
    assert(local_command_sync_host_request_count() == 1);
    assert(
        strcmp(
            local_command_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "local-commands"
        ) == 0
    );
    assert(
        strcmp(
            local_command_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(local_command_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(local_command_count() == 2);

    local_command_t resolved = {};
    assert(local_command_resolve(" 播放睡前故事 ", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_PLAY_PACKAGE);
    assert(strcmp(resolved.argument, "content_story_001") == 0);
    // The per-command revision is absent, so it inherits the set revision.
    assert(resolved.revision == 9u);
    assert(local_command_resolve("停止播放", &resolved));
    assert(!local_command_resolve("本地口令", &resolved));
}

void test_empty_snapshot_clears_commands() {
    reset_all();
    seed_command("cmd_local", "本地口令", LOCAL_COMMAND_ACTION_STOP, nullptr);
    set_single_response(
        200,
        "{\"data\":{\"revision\":2,\"commands\":[]}}"
    );

    assert(local_command_sync_now() == ESP_OK);
    assert(local_command_count() == 0);
}

void test_partial_snapshot_keeps_valid_entries() {
    reset_all();
    set_single_response(
        200,
        "{\"data\":{\"revision\":3,\"commands\":["
        "{\"command_id\":\"cmd_play\",\"phrase\":\"播放\","
        "\"action\":\"play_package\",\"argument\":\"story_001\"},"
        "{\"command_id\":\"cmd_bad\",\"phrase\":\"坏命令\","
        "\"action\":\"play_package\"},"
        "{\"command_id\":\"cmd_play\",\"phrase\":\"重复命令\","
        "\"action\":\"stop\"}]}}"
    );

    assert(local_command_sync_now() == ESP_OK);
    assert(local_command_count() == 1);
    local_command_t resolved = {};
    assert(local_command_resolve("播放", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_PLAY_PACKAGE);
    assert(!local_command_resolve("坏命令", &resolved));
}

void test_malformed_responses_leave_commands_untouched() {
    reset_all();
    seed_command("cmd_local", "本地口令", LOCAL_COMMAND_ACTION_STOP, nullptr);

    set_single_response(200, "{not-json");
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_count() == 1);

    set_single_response(200, "{}");
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_count() == 1);

    set_single_response(
        200,
        "{\"data\":{\"commands\":[{\"command_id\":\"\","
        "\"phrase\":\"无编号\",\"action\":\"stop\"}]}}"
    );
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_count() == 1);

    set_single_response(500, "{}");
    assert(local_command_sync_now() == ESP_FAIL);
    assert(local_command_count() == 1);
}

void test_oversized_response_is_rejected() {
    reset_all();
    seed_command("cmd_local", "本地口令", LOCAL_COMMAND_ACTION_STOP, nullptr);
    static char oversized[33000];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';

    set_single_response(200, oversized);
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_count() == 1);
}

void test_oversized_snapshot_returns_state_error_and_keeps_commands() {
    reset_all();
    seed_command("cmd_local", "本地口令", LOCAL_COMMAND_ACTION_STOP, nullptr);

    char body[4096] = {};
    const int prefix = snprintf(
        body,
        sizeof(body),
        "{\"data\":{\"revision\":7,\"commands\":["
    );
    assert(prefix > 0);
    size_t used = (size_t)prefix;
    for (int index = 0; index < LOCAL_COMMAND_CAPACITY + 1; ++index) {
        const int written = snprintf(
            body + used,
            sizeof(body) - used,
            "%s{\"command_id\":\"cmd_%03d\",\"phrase\":\"命令 %03d\","
            "\"action\":\"stop\"}",
            index == 0 ? "" : ",",
            index,
            index
        );
        assert(written > 0);
        used += (size_t)written;
        assert(used < sizeof(body));
    }
    assert(used + 4 < sizeof(body));
    snprintf(body + used, sizeof(body) - used, "]}}");

    set_single_response(200, body);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_count() == 1);
    local_command_t resolved = {};
    assert(local_command_resolve("本地口令", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_STOP);
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);
    assert(local_command_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    local_command_sync_host_set_client_init_fails(true);
    assert(local_command_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    local_command_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(local_command_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    local_command_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(local_command_sync_now() == ESP_ERR_INVALID_ARG);
    assert(local_command_sync_host_request_count() == 0);
    assert(local_command_sync_host_transport_security_calls() == 1);
}

void test_initialize_requires_store_and_is_idempotent() {
    local_command_sync_host_reset();
    local_command_sync_host_reset_module();
    local_command_sync_host_reset_store();
    local_command_test_reset_ready();
    const module_descriptor_t *descriptor =
        local_command_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!local_command_sync_host_is_ready());
    assert(local_command_sync_host_task_create_calls() == 0);

    assert(local_command_init() == ESP_OK);
    local_command_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!local_command_sync_host_is_ready());

    local_command_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(local_command_sync_host_is_ready());
    assert(local_command_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(local_command_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        local_command_sync_module_descriptor();
    local_command_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    local_command_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(local_command_sync_host_task_pending());

    local_command_sync_host_run_pending_task();
    assert(local_command_sync_host_request_count() == 0);
    assert(local_command_sync_host_delay_calls() == 1);
    assert(local_command_sync_host_last_delay_ticks() == 15000u);
    assert(local_command_sync_host_task_delete_calls() == 1);
    assert(!local_command_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        local_command_sync_module_descriptor();
    set_single_response(
        200,
        "{\"data\":{\"revision\":1,\"commands\":[]}}"
    );
    local_command_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    local_command_sync_host_run_pending_task();
    assert(local_command_sync_host_request_count() == 1);
    assert(local_command_sync_host_delay_calls() == 1);
    assert(local_command_sync_host_last_delay_ticks() == 900000u);
    assert(local_command_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_replaces_command_set();
    test_empty_snapshot_clears_commands();
    test_partial_snapshot_keeps_valid_entries();
    test_malformed_responses_leave_commands_untouched();
    test_oversized_response_is_rejected();
    test_oversized_snapshot_returns_state_error_and_keeps_commands();
    test_unauthorized_fetch_clears_session();
    test_transport_failures_are_propagated();
    test_initialize_requires_store_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_syncs_then_uses_regular_interval();
    return 0;
}
