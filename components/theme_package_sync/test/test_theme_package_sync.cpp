#include "theme_package_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "network_manager.h"
#include "theme_package.h"
#include "theme_package_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    theme_package_test_reset_ready();
    theme_package_sync_host_reset_module();
    theme_package_sync_host_reset();
    theme_package_init();
}

theme_pack_t local_pack(
    const char *theme_id,
    const char *title,
    int64_t revision
) {
    theme_pack_t pack = {};
    strncpy(pack.theme_id, theme_id, sizeof(pack.theme_id) - 1);
    strncpy(pack.title, title, sizeof(pack.title) - 1);
    strncpy(pack.package_ids[0], "content_story_001",
            sizeof(pack.package_ids[0]) - 1);
    pack.member_count = 1;
    pack.catalog_revision = revision;
    return pack;
}

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        theme_package_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "theme_package_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_ready_cache() {
    theme_package_test_reset_ready();
    theme_package_sync_host_reset_module();
    theme_package_sync_host_reset();
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_request_count() == 0);
}

void test_fetch_rejects_unusable_runtime_state() {
    reset_all();
    theme_package_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_request_count() == 0);

    reset_all();
    theme_package_sync_host_set_time_synchronized(false);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_request_count() == 0);

    reset_all();
    theme_package_sync_host_set_registered(false);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_request_count() == 0);
}

void test_successful_fetch_replaces_cache_and_uses_contract() {
    reset_all();
    const theme_pack_t stale = local_pack("theme_stale", "旧主题", 1);
    assert(theme_package_upsert(&stale) == ESP_OK);

    const char *const body =
        "{\"data\":{\"theme_packs\":["
        "{\"theme_id\":\"theme_ocean\",\"title\":\"海洋主题\","
        "\"package_ids\":[\"content_story_001\",\"content_story_002\"],"
        "\"catalog_revision\":7},"
        "{\"theme_id\":\"theme_night\",\"title\":\"睡前主题\","
        "\"package_ids\":[\"content_song_003\"],"
        "\"catalog_revision\":9}"
        "]}}";
    const int statuses[] = {200};
    const char *bodies[] = {body};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);

    assert(theme_package_sync_now() == ESP_OK);
    assert(theme_package_sync_host_request_count() == 1);
    assert(
        strcmp(
            theme_package_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "content/theme-packs"
        ) == 0
    );
    assert(
        strcmp(
            theme_package_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(theme_package_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(theme_package_count() == 2);

    theme_pack_t ocean = {};
    assert(theme_package_get("theme_ocean", &ocean));
    assert(strcmp(ocean.title, "海洋主题") == 0);
    assert(ocean.member_count == 2);
    assert(ocean.catalog_revision == 7);
    size_t member_index = 0;
    assert(
        theme_package_contains_package(
            "theme_ocean",
            "content_story_002",
            &member_index
        )
    );
    assert(member_index == 1);
}

void test_empty_snapshot_clears_cache() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{\"theme_packs\":[]}}"};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_OK);
    assert(theme_package_count() == 0);
}

void test_nonempty_invalid_snapshot_is_rejected() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    const int statuses[] = {200};
    const char *bodies[] = {
        "{\"data\":{\"theme_packs\":["
        "{\"theme_id\":\"\",\"title\":\"缺少编号\","
        "\"package_ids\":[\"content_story_001\"]},"
        "{\"theme_id\":\"theme_empty\",\"title\":\"缺少成员\","
        "\"package_ids\":[]}"
        "]}}",
    };
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
    assert(theme_package_count() == 1);
    theme_pack_t cached_after_reject = {};
    assert(theme_package_get("theme_cached", &cached_after_reject));
    assert(strcmp(cached_after_reject.theme_id, "theme_cached") == 0);
}

void test_malformed_json_is_rejected_without_touching_cache() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    const int statuses[] = {200};
    const char *bodies[] = {"not-json"};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
    assert(theme_package_count() == 1);
}

void test_missing_data_array_is_rejected() {
    reset_all();
    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{}}"};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
}

void test_member_overflow_rejects_snapshot_and_keeps_cache() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    // 17 members exceeds THEME_PACKAGE_MEMBER_CAPACITY (16). The snapshot is
    // rejected as a whole so the device never silently drops published
    // members from the theme menu.
    const char *const body =
        "{\"data\":{\"theme_packs\":["
        "{\"theme_id\":\"theme_big\",\"title\":\"超限主题\","
        "\"package_ids\":["
        "\"content_story_001\",\"content_story_002\","
        "\"content_story_003\",\"content_story_004\","
        "\"content_story_005\",\"content_story_006\","
        "\"content_story_007\",\"content_story_008\","
        "\"content_story_009\",\"content_story_010\","
        "\"content_story_011\",\"content_story_012\","
        "\"content_story_013\",\"content_story_014\","
        "\"content_story_015\",\"content_story_016\","
        "\"content_story_017\"],"
        "\"catalog_revision\":5}"
        "]}}";
    const int statuses[] = {200};
    const char *bodies[] = {body};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);

    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
    assert(theme_package_count() == 1);
    theme_pack_t preserved = {};
    assert(theme_package_get("theme_cached", &preserved));
}

void test_member_capacity_boundary_is_accepted() {
    reset_all();
    const char *const body =
        "{\"data\":{\"theme_packs\":["
        "{\"theme_id\":\"theme_full\",\"title\":\"满容量主题\","
        "\"package_ids\":["
        "\"content_story_001\",\"content_story_002\","
        "\"content_story_003\",\"content_story_004\","
        "\"content_story_005\",\"content_story_006\","
        "\"content_story_007\",\"content_story_008\","
        "\"content_story_009\",\"content_story_010\","
        "\"content_story_011\",\"content_story_012\","
        "\"content_story_013\",\"content_story_014\","
        "\"content_story_015\",\"content_story_016\"],"
        "\"catalog_revision\":6}"
        "]}}";
    const int statuses[] = {200};
    const char *bodies[] = {body};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);

    assert(theme_package_sync_now() == ESP_OK);
    theme_pack_t full = {};
    assert(theme_package_get("theme_full", &full));
    assert(full.member_count == THEME_PACKAGE_MEMBER_CAPACITY);
}

void test_pack_overflow_returns_retryable_error_and_keeps_cache() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    char body[4096] = {};
    const int prefix = snprintf(
        body,
        sizeof(body),
        "{\"data\":{\"theme_packs\":["
    );
    assert(prefix > 0);
    size_t used = (size_t)prefix;
    for (int index = 0; index < THEME_PACKAGE_CAPACITY + 1; ++index) {
        const int written = snprintf(
            body + used,
            sizeof(body) - used,
            "%s{\"theme_id\":\"theme_%03d\",\"title\":\"主题 %03d\","
            "\"package_ids\":[\"content_story_001\"],"
            "\"catalog_revision\":1}",
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

    const int statuses[] = {200};
    const char *bodies[] = {body};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_count() == 1);
    theme_pack_t preserved = {};
    assert(theme_package_get("theme_cached", &preserved));
}

void test_invalid_member_rejects_snapshot_and_keeps_cache() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    const int statuses[] = {200};
    const char *bodies[] = {
        "{\"data\":{\"theme_packs\":["
        "{\"theme_id\":\"theme_invalid\",\"title\":\"异常主题\","
        "\"package_ids\":[\"content_story_001\",\"\"]}"
        "]}}",
    };
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
    assert(theme_package_count() == 1);
    theme_pack_t preserved = {};
    assert(theme_package_get("theme_cached", &preserved));
}

void test_unauthorized_clears_session() {
    reset_all();
    const int statuses[] = {401};
    const char *bodies[] = {"{}"};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_clear_session_calls() == 1);

    reset_all();
    const int forbidden_statuses[] = {403};
    const char *forbidden_bodies[] = {"{}"};
    theme_package_sync_host_set_http_sequence(
        forbidden_statuses,
        forbidden_bodies,
        1
    );
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_clear_session_calls() == 1);
}

void test_identity_base_url_and_transport_failures() {
    reset_all();
    theme_package_sync_host_set_copy_session_result(ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_request_count() == 0);

    reset_all();
    theme_package_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_sync_host_request_count() == 0);

    reset_all();
    theme_package_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(theme_package_sync_host_request_count() == 0);

    reset_all();
    theme_package_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
    assert(theme_package_sync_host_request_count() == 0);

    reset_all();
    theme_package_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_HOST_NOT_ALLOWED
    );
    assert(theme_package_sync_now() == ESP_ERR_INVALID_ARG);
    assert(theme_package_sync_host_request_count() == 0);
    assert(theme_package_sync_host_transport_security_calls() == 1);
}

void test_transport_and_server_failures_do_not_replace_cache() {
    reset_all();
    const theme_pack_t cached = local_pack("theme_cached", "缓存主题", 3);
    assert(theme_package_upsert(&cached) == ESP_OK);

    theme_package_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(theme_package_sync_now() == ESP_ERR_INVALID_STATE);
    assert(theme_package_count() == 1);

    reset_all();
    assert(theme_package_upsert(&cached) == ESP_OK);
    const int statuses[] = {500};
    const char *bodies[] = {"{}"};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(theme_package_sync_now() == ESP_FAIL);
    assert(theme_package_count() == 1);
}

void test_initialize_requires_cache_and_is_idempotent() {
    theme_package_test_reset_ready();
    theme_package_sync_host_reset_module();
    theme_package_sync_host_reset();
    const module_descriptor_t *const descriptor =
        theme_package_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!theme_package_sync_host_is_ready());
    assert(theme_package_sync_host_task_create_calls() == 0);

    assert(theme_package_init() == ESP_OK);
    theme_package_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!theme_package_sync_host_is_ready());

    theme_package_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(theme_package_sync_host_is_ready());
    assert(theme_package_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(theme_package_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        theme_package_sync_module_descriptor();
    theme_package_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    theme_package_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(theme_package_sync_host_task_pending());

    theme_package_sync_host_run_pending_task();
    assert(theme_package_sync_host_request_count() == 0);
    assert(theme_package_sync_host_delay_calls() == 1);
    assert(theme_package_sync_host_last_delay_ticks() == 15000u);
    assert(theme_package_sync_host_task_delete_calls() == 1);
    assert(!theme_package_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        theme_package_sync_module_descriptor();
    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{\"theme_packs\":[]}}"};
    theme_package_sync_host_set_http_sequence(statuses, bodies, 1);
    theme_package_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    theme_package_sync_host_run_pending_task();
    assert(theme_package_sync_host_request_count() == 1);
    assert(theme_package_sync_host_delay_calls() == 1);
    assert(theme_package_sync_host_last_delay_ticks() == 1800000u);
    assert(theme_package_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_ready_cache();
    test_fetch_rejects_unusable_runtime_state();
    test_successful_fetch_replaces_cache_and_uses_contract();
    test_empty_snapshot_clears_cache();
    test_nonempty_invalid_snapshot_is_rejected();
    test_malformed_json_is_rejected_without_touching_cache();
    test_missing_data_array_is_rejected();
    test_member_overflow_rejects_snapshot_and_keeps_cache();
    test_member_capacity_boundary_is_accepted();
    test_pack_overflow_returns_retryable_error_and_keeps_cache();
    test_invalid_member_rejects_snapshot_and_keeps_cache();
    test_unauthorized_clears_session();
    test_identity_base_url_and_transport_failures();
    test_transport_and_server_failures_do_not_replace_cache();
    test_initialize_requires_cache_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_syncs_then_uses_regular_interval();
    return 0;
}
