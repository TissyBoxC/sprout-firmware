#include "long_term_memory_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "long_term_memory.h"
#include "long_term_memory_sync_host.h"
#include "transport_security.h"

namespace {

const char *const PULL_PATH = "/memories?since_revision=";
const char *const UPLOAD_PATH = "/memories";

long_term_memory_entry_t make_entry(
    const char *key,
    const char *value,
    int64_t updated_at
) {
    long_term_memory_entry_t entry = {};
    std::strncpy(entry.entry_id, key, LONG_TERM_MEMORY_ID_SIZE - 1);
    std::strncpy(
        entry.child_id,
        "child_001",
        LONG_TERM_MEMORY_CHILD_ID_SIZE - 1
    );
    std::strncpy(entry.key, key, LONG_TERM_MEMORY_KEY_SIZE - 1);
    std::strncpy(entry.value, value, LONG_TERM_MEMORY_VALUE_SIZE - 1);
    entry.updated_at_epoch_seconds = updated_at;
    return entry;
}

void reset_all(bool is_authorized) {
    long_term_memory_sync_host_reset_module();
    long_term_memory_sync_host_reset();
    long_term_memory_sync_host_reset_store();
    assert(long_term_memory_init() == ESP_OK);
    assert(long_term_memory_set_authorized(is_authorized) == ESP_OK);
    // long_term_memory_sync_now() is only meaningful once the worker module
    // has initialized, exactly as the module registry drives it in production.
    assert(
        long_term_memory_sync_module_descriptor()->initialize() == ESP_OK
    );
}

void set_responses(const int *statuses, const char *const *bodies, size_t count) {
    long_term_memory_sync_host_set_http_sequence(statuses, bodies, count);
}

void set_pull_response(const char *body) {
    const int statuses[] = {200};
    const char *bodies[] = {body};
    set_responses(statuses, bodies, 1);
}

void set_pull_then_upload(const char *pull_body, const char *upload_body) {
    const int statuses[] = {200, 200};
    const char *bodies[] = {pull_body, upload_body};
    set_responses(statuses, bodies, 2);
}

size_t count_request_path(const char *fragment) {
    size_t matches = 0;
    for (size_t index = 0; index < long_term_memory_sync_host_request_count();
         ++index) {
        if (std::strstr(
                long_term_memory_sync_host_request_url(index),
                fragment
            ) != nullptr) {
            matches += 1;
        }
    }
    return matches;
}

long_term_memory_sync_status_snapshot_t status() {
    long_term_memory_sync_status_snapshot_t snapshot = {};
    assert(long_term_memory_sync_get_status(&snapshot) == ESP_OK);
    return snapshot;
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "long_term_memory_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_store() {
    long_term_memory_sync_host_reset_module();
    long_term_memory_sync_host_reset();
    long_term_memory_sync_host_reset_store();
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_sync_host_request_count() == 0);
}

void test_now_requires_consent() {
    reset_all(false);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_sync_host_request_count() == 0);
    assert(status().status == LONG_TERM_MEMORY_SYNC_STATUS_UNAUTHORIZED);
}

void test_now_requires_trusted_runtime_state() {
    reset_all(true);
    long_term_memory_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);

    long_term_memory_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_CONNECTED
    );
    long_term_memory_sync_host_set_time_synchronized(false);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);

    long_term_memory_sync_host_set_time_synchronized(true);
    long_term_memory_sync_host_set_registered(false);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all(true);
    long_term_memory_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_sync_host_request_count() == 0);

    reset_all(true);
    long_term_memory_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_sync_host_request_count() == 0);

    reset_all(true);
    long_term_memory_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_sync_host_request_count() == 0);
}

void test_full_pull_merges_remote_memories() {
    reset_all(true);
    set_pull_response(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":["
        "{\"memory_key\":\"favorite_color\",\"memory_value\":\"蓝色\","
        "\"confidence\":1.0,\"revision\":4,\"updated_at\":1700000100},"
        "{\"memory_key\":\"pet_name\",\"memory_value\":\"布丁\","
        "\"confidence\":0.9,\"revision\":5,\"updated_at\":1700000200}"
        "],\"revision\":5}}"
    );

    assert(long_term_memory_sync_now() == ESP_OK);
    assert(long_term_memory_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            long_term_memory_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "memories?since_revision=0"
        ) == 0
    );
    assert(
        std::strcmp(
            long_term_memory_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        long_term_memory_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(count_request_path(UPLOAD_PATH) == 1);

    long_term_memory_entry_t recalled = {};
    // Memories are keyed by the child the server named, not by this device, so
    // the same child keeps one shared memory across devices.
    assert(
        long_term_memory_recall(
            "child-1",
            "favorite_color",
            &recalled
        )
    );
    assert(std::strcmp(recalled.value, "蓝色") == 0);
    assert(recalled.updated_at_epoch_seconds == 1700000100);
    assert(long_term_memory_recall("child-1", "pet_name", &recalled));
    assert(std::strcmp(recalled.value, "布丁") == 0);
    // The device id must never become a memory owner, or two devices serving
    // the same child would each keep a private copy.
    assert(!long_term_memory_recall("device-0001", "favorite_color", &recalled));

    const long_term_memory_sync_status_snapshot_t after = status();
    assert(after.status == LONG_TERM_MEMORY_SYNC_STATUS_IDLE);
    assert(after.revision == 5);
    assert(after.has_snapshot);
    assert(after.pending_count == 0);
}

// A guardian deleting one memory leaves no live row, so the only signal the
// device gets is deleted_keys; the revoked fact must stop being recalled.
void test_deleted_keys_remove_local_memory() {
    reset_all(true);
    set_pull_response(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"pet_name\","
        "\"memory_value\":\"布丁\",\"confidence\":1.0,\"revision\":7,"
        "\"updated_at\":1700000200}],\"revision\":7}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);

    long_term_memory_entry_t recalled = {};
    assert(long_term_memory_recall("child-1", "pet_name", &recalled));

    long_term_memory_sync_host_reset();
    set_pull_response(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[],"
        "\"deleted_keys\":[\"pet_name\"],\"revision\":8}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);

    assert(!long_term_memory_recall("child-1", "pet_name", &recalled));
    assert(status().revision == 8);
}

void test_second_pull_uses_since_revision() {    reset_all(true);
    set_pull_response(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"pet_name\","
        "\"memory_value\":\"布丁\",\"confidence\":1.0,\"revision\":7,"
        "\"updated_at\":1700000200}],\"revision\":7}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);
    assert(status().revision == 7);

    long_term_memory_sync_host_reset();
    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":7}}");
    assert(long_term_memory_sync_now() == ESP_OK);
    assert(
        std::strstr(
            long_term_memory_sync_host_request_url(0),
            "since_revision=7"
        ) != nullptr
    );
}

void test_empty_snapshot_clears_local_memories() {
    reset_all(true);
    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);
    assert(long_term_memory_count() == 1);

    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":9}}");
    assert(long_term_memory_sync_now() == ESP_OK);

    assert(long_term_memory_count() == 0);
    long_term_memory_entry_t recalled = {};
    assert(
        !long_term_memory_recall("child_001", "favorite_color", &recalled)
    );
    assert(status().revision == 9);
}

void test_unusable_payload_keeps_local_memories() {
    reset_all(true);
    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);

    set_pull_response("{not-json");
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_count() == 1);

    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"\"}]}}");
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_count() == 1);

    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"revision\":3}}");
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_count() == 1);

    const int statuses[] = {500};
    const char *bodies[] = {"{}"};
    set_responses(statuses, bodies, 1);
    assert(long_term_memory_sync_now() == ESP_FAIL);
    assert(long_term_memory_count() == 1);

    // The failed pulls must not have advanced the cursor.
    assert(!status().has_snapshot);
    assert(status().revision == 0);
}

void test_network_failure_keeps_local_memories() {
    reset_all(true);
    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);

    long_term_memory_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_count() == 1);
    assert(status().status == LONG_TERM_MEMORY_SYNC_STATUS_OFFLINE);
}

void test_clock_loss_keeps_local_memories() {
    reset_all(true);
    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);

    long_term_memory_sync_host_set_time_synchronized(false);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_count() == 1);
    assert(long_term_memory_sync_host_request_count() == 0);
}

void test_unauthorized_fetch_clears_session() {
    reset_all(true);
    const int status401[] = {401};
    const char *body401[] = {"{}"};
    set_responses(status401, body401, 1);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_sync_host_clear_session_calls() == 1);

    reset_all(true);
    const int status403[] = {403};
    const char *body403[] = {"{}"};
    set_responses(status403, body403, 1);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all(true);
    long_term_memory_sync_host_set_client_init_fails(true);
    assert(long_term_memory_sync_now() == ESP_ERR_NO_MEM);

    reset_all(true);
    long_term_memory_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_sync_host_request_count() == 0);
}

void test_oversized_snapshot_is_rejected() {
    reset_all(true);
    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);

    char body[8192] = {};
    size_t used = 0;
    const int prefix = snprintf(
        body,
        sizeof(body),
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":["
    );
    assert(prefix > 0);
    used = (size_t)prefix;
    for (int index = 0; index < LONG_TERM_MEMORY_CAPACITY + 1; ++index) {
        const int written = snprintf(
            body + used,
            sizeof(body) - used,
            "%s{\"memory_key\":\"memory_%03d\",\"memory_value\":\"v\","
            "\"confidence\":1.0,\"revision\":1,\"updated_at\":0}",
            index == 0 ? "" : ",",
            index
        );
        assert(written > 0);
        used += (size_t)written;
        assert(used < sizeof(body));
    }
    snprintf(body + used, sizeof(body) - used, "],\"revision\":11}}");

    set_pull_response(body);
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_STATE);
    assert(long_term_memory_count() == 1);
    assert(!status().has_snapshot);
}

void test_stale_revision_is_rejected() {
    reset_all(true);
    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":12}}");
    assert(long_term_memory_sync_now() == ESP_OK);
    assert(status().revision == 12);

    long_term_memory_sync_host_reset();
    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":3}}");
    assert(long_term_memory_sync_now() == ESP_ERR_INVALID_ARG);
    assert(status().revision == 12);
}

void test_local_entries_win_over_remote_snapshot() {
    reset_all(true);
    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "绿色",
        1700000500
    );
    assert(long_term_memory_remember(&local) == ESP_OK);

    set_pull_response(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"favorite_color\","
        "\"memory_value\":\"蓝色\",\"confidence\":1.0,\"revision\":2,"
        "\"updated_at\":1700000000}],\"revision\":2}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);

    long_term_memory_entry_t recalled = {};
    assert(long_term_memory_recall("child_001", "favorite_color", &recalled));
    assert(std::strcmp(recalled.value, "绿色") == 0);
}

void test_local_memory_is_uploaded_then_marked_synced() {
    reset_all(true);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_OK);

    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);
    assert(
        long_term_memory_sync_enqueue_key("favorite_color") == ESP_OK
    );
    assert(long_term_memory_sync_host_is_pending("favorite_color"));
    assert(status().pending_count == 1);

    set_pull_then_upload(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"pet_name\","
        "\"memory_value\":\"布丁\",\"confidence\":1.0,\"revision\":3,"
        "\"updated_at\":1700000200}],\"revision\":3}}",
        "{\"data\":{\"child_id\":\"child-1\",\"revision\":4,\"accepted\":1}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);

    assert(long_term_memory_sync_host_request_count() == 2);
    assert(
        long_term_memory_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        std::strcmp(
            long_term_memory_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/memories"
        ) == 0
    );
    const char *request_body =
        long_term_memory_sync_host_request_body(1);
    assert(std::strstr(request_body, "\"memory_key\":\"favorite_color\""));
    assert(std::strstr(request_body, "\"memory_value\":\"蓝色\""));
    // The serializer emits a whole-valued float as 1, which JSON reads as the
    // number 1 and the platform accepts as full weight; the Q16 form the
    // device uses internally must never reach the wire.
    assert(std::strstr(request_body, "\"confidence\":1"));
    assert(std::strstr(request_body, "65536") == nullptr);
    assert(std::strstr(request_body, "pet_name") == nullptr);

    assert(!long_term_memory_sync_host_is_pending("favorite_color"));
    const long_term_memory_sync_status_snapshot_t after = status();
    assert(after.pending_count == 0);
    assert(after.revision == 4);
}

void test_rejected_upload_keeps_the_memory_pending() {
    reset_all(true);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_OK);

    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);
    assert(long_term_memory_sync_enqueue_key("favorite_color") == ESP_OK);

    const int statuses[] = {200, 500};
    const char *bodies[] = {
        // A non-empty snapshot keeps the locally produced entry in place; an
        // empty snapshot would legitimately clear it before the upload runs.
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"pet_name\","
        "\"memory_value\":\"布丁\",\"confidence\":1.0,\"revision\":2,"
        "\"updated_at\":1700000200}],\"revision\":2}}",
        "{}",
    };
    set_responses(statuses, bodies, 2);
    assert(long_term_memory_sync_now() == ESP_FAIL);
    assert(long_term_memory_sync_host_is_pending("favorite_color"));
    assert(status().pending_count == 1);
    // The pull half succeeded, so the cursor still advances.
    assert(status().revision == 2);

    long_term_memory_sync_host_reset();
    set_pull_then_upload(
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"pet_name\","
        "\"memory_value\":\"布丁\",\"confidence\":1.0,\"revision\":2,"
        "\"updated_at\":1700000200}],\"revision\":2}}",
        "{\"data\":{\"child_id\":\"child-1\",\"revision\":4,\"accepted\":1}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);
    assert(!long_term_memory_sync_host_is_pending("favorite_color"));
}

void test_partial_acceptance_keeps_the_batch_pending() {
    reset_all(true);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_OK);

    const long_term_memory_entry_t first = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    const long_term_memory_entry_t second = make_entry(
        "pet_name",
        "布丁",
        1700000001
    );
    assert(long_term_memory_remember(&first) == ESP_OK);
    assert(long_term_memory_remember(&second) == ESP_OK);
    assert(long_term_memory_sync_enqueue_key("favorite_color") == ESP_OK);
    assert(long_term_memory_sync_enqueue_key("pet_name") == ESP_OK);

    set_pull_then_upload(
        // A non-empty snapshot keeps both local entries available for upload;
        // an empty one would clear them first.
        "{\"data\":{\"child_id\":\"child-1\",\"memories\":[{\"memory_key\":\"favorite_food\","
        "\"memory_value\":\"面条\",\"confidence\":1.0,\"revision\":5,"
        "\"updated_at\":1700000300}],\"revision\":5}}",
        "{\"data\":{\"child_id\":\"child-1\",\"revision\":6,\"accepted\":1}}"
    );
    assert(long_term_memory_sync_now() == ESP_OK);

    // A partially accepted batch is never marked synced, and the cursor stays
    // at the pulled revision because the server revision covers both.
    assert(long_term_memory_sync_host_is_pending("favorite_color"));
    assert(long_term_memory_sync_host_is_pending("pet_name"));
    assert(status().revision == 5);
}

void test_forgotten_memory_leaves_the_pending_queue() {
    reset_all(true);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_OK);

    const long_term_memory_entry_t local = make_entry(
        "favorite_color",
        "蓝色",
        1700000000
    );
    assert(long_term_memory_remember(&local) == ESP_OK);
    assert(long_term_memory_sync_enqueue_key("favorite_color") == ESP_OK);
    assert(long_term_memory_forget("favorite_color") == ESP_OK);

    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":1}}");
    assert(long_term_memory_sync_now() == ESP_OK);

    // Nothing is uploaded for a key whose local entry is gone.
    assert(count_request_path("POST") == 0);
    assert(long_term_memory_sync_host_request_count() == 1);
    assert(!long_term_memory_sync_host_is_pending("favorite_color"));
}

void test_enqueue_rejects_unusable_keys() {
    reset_all(true);
    assert(long_term_memory_sync_enqueue_key(nullptr) == ESP_ERR_INVALID_ARG);
    assert(long_term_memory_sync_enqueue_key("") == ESP_ERR_INVALID_ARG);
    assert(
        long_term_memory_sync_enqueue_key("has space") == ESP_ERR_INVALID_ARG
    );
    assert(long_term_memory_sync_enqueue_key("ok_key") == ESP_OK);
    assert(long_term_memory_sync_enqueue_key("ok_key") == ESP_OK);
    assert(status().pending_count == 1);
}

void test_enqueue_requires_initialization() {
    long_term_memory_sync_host_reset_module();
    long_term_memory_sync_host_reset();
    long_term_memory_sync_host_reset_store();
    assert(
        long_term_memory_sync_enqueue_key("favorite_color") ==
        ESP_ERR_INVALID_STATE
    );
}

void test_initialize_requires_store_and_is_idempotent() {
    // This case must run before any other test initializes long_term_memory,
    // because that module has no reset hook and its readiness is sticky.
    long_term_memory_sync_host_reset_module();
    long_term_memory_sync_host_reset();
    long_term_memory_sync_host_reset_store();
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!long_term_memory_sync_host_is_ready());
    assert(long_term_memory_sync_host_task_create_calls() == 0);

    assert(long_term_memory_init() == ESP_OK);
    long_term_memory_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!long_term_memory_sync_host_is_ready());

    long_term_memory_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(long_term_memory_sync_host_is_ready());
    assert(descriptor->initialize() == ESP_OK);
    assert(long_term_memory_sync_host_task_create_calls() == 2);
    assert(long_term_memory_sync_get_status(nullptr) == ESP_ERR_INVALID_ARG);
}

void test_worker_retries_offline_and_stops_cleanly() {
    reset_all(true);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    long_term_memory_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    long_term_memory_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(long_term_memory_sync_host_task_pending());

    long_term_memory_sync_host_run_pending_task();
    assert(long_term_memory_sync_host_request_count() == 0);
    assert(long_term_memory_sync_host_delay_calls() == 1);
    assert(long_term_memory_sync_host_task_delete_calls() == 1);
    assert(!long_term_memory_sync_host_task_pending());
}

void test_worker_syncs_then_stops_cleanly() {
    reset_all(true);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":1}}");
    long_term_memory_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    long_term_memory_sync_host_run_pending_task();
    assert(long_term_memory_sync_host_request_count() == 1);
    assert(long_term_memory_sync_host_delay_calls() == 1);
    assert(long_term_memory_sync_host_task_delete_calls() == 1);
}

void test_state_survives_reinitialization() {
    reset_all(true);
    set_pull_response("{\"data\":{\"child_id\":\"child-1\",\"memories\":[],\"revision\":21}}");
    assert(long_term_memory_sync_now() == ESP_OK);
    assert(status().revision == 21);

    // A restart reloads the persisted cursor and pending queue from the store.
    long_term_memory_sync_host_reset_module();
    long_term_memory_sync_host_reset();
    assert(long_term_memory_init() == ESP_OK);
    const module_descriptor_t *descriptor =
        long_term_memory_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_OK);
    assert(status().revision == 21);
    assert(status().has_snapshot);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    // Readiness-gated cases run before anything initializes long_term_memory,
    // because that module's ready flag is sticky by contract.
    test_now_requires_initialized_store();
    test_initialize_requires_store_and_is_idempotent();
    test_enqueue_requires_initialization();
    test_now_requires_consent();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_full_pull_merges_remote_memories();
    test_deleted_keys_remove_local_memory();
    test_second_pull_uses_since_revision();
    test_empty_snapshot_clears_local_memories();
    test_unusable_payload_keeps_local_memories();
    test_network_failure_keeps_local_memories();
    test_clock_loss_keeps_local_memories();
    test_unauthorized_fetch_clears_session();
    test_transport_failures_are_propagated();
    test_oversized_snapshot_is_rejected();
    test_stale_revision_is_rejected();
    test_local_entries_win_over_remote_snapshot();
    test_local_memory_is_uploaded_then_marked_synced();
    test_rejected_upload_keeps_the_memory_pending();
    test_partial_acceptance_keeps_the_batch_pending();
    test_forgotten_memory_leaves_the_pending_queue();
    test_enqueue_rejects_unusable_keys();
    test_worker_retries_offline_and_stops_cleanly();
    test_worker_syncs_then_stops_cleanly();
    test_state_survives_reinitialization();
    std::printf("long_term_memory_sync_runtime: all checks passed\n");
    return 0;
}
