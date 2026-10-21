#include "long_term_memory_sync.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "long_term_memory.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"

#include "long_term_memory_sync_core.h"
#include "long_term_memory_sync_store.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define LONG_TERM_MEMORY_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define LONG_TERM_MEMORY_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

// Kconfig defaults mirror the fallbacks in every sibling sync component so a
// host test build without a generated sdkconfig still compiles.
#ifndef CONFIG_LONG_TERM_MEMORY_SYNC_INTERVAL_SECONDS
#define CONFIG_LONG_TERM_MEMORY_SYNC_INTERVAL_SECONDS 900
#endif
#ifndef CONFIG_LONG_TERM_MEMORY_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_LONG_TERM_MEMORY_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define LONG_TERM_MEMORY_SYNC_URL_SIZE 384
// 16 entries x (31-byte key + 159-byte value + envelope) never fills 16 KiB on
// their own; the cap exists for JSON escaping and a runaway server response.
#define LONG_TERM_MEMORY_SYNC_RESPONSE_SIZE 16384
// 8 entries x ~220 bytes of JSON plus escaping stay far below this bound.
#define LONG_TERM_MEMORY_SYNC_REQUEST_SIZE 4096

// The platform rejects keys longer than this and trims surrounding space.
// Matching the limit locally keeps a rejected batch from blocking the queue.
#define LONG_TERM_MEMORY_SYNC_KEY_MAX_LENGTH 64

static const char *const TAG = "ltm_sync";

static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;
static SemaphoreHandle_t s_lock;
static long_term_memory_sync_store_t s_store;
static long_term_memory_sync_status_t s_status;
static int64_t s_last_success_epoch_seconds;

/**
 * @brief Growable HTTP response body.
 *
 * A pull can carry every memory value, so unlike the snapshot syncs the body
 * grows on demand up to a hard cap; a cap hit is reported so the caller
 * rejects the payload instead of truncating it.
 */
typedef struct {
    char *body;
    size_t length;
    size_t capacity;
    bool overflowed;
} long_term_memory_sync_response_t;

static void long_term_memory_sync_response_destroy(
    long_term_memory_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static bool long_term_memory_sync_response_append(
    long_term_memory_sync_response_t *response,
    const char *data,
    size_t data_length
) {
    const size_t needed = response->length + data_length + 1;
    if (needed > response->capacity) {
        size_t capacity = response->capacity == 0 ? 2048 : response->capacity;
        while (capacity < needed) {
            capacity *= 2;
        }
        if (capacity > LONG_TERM_MEMORY_SYNC_RESPONSE_SIZE) {
            response->overflowed = true;
            return false;
        }
        char *grown = (char *)realloc(response->body, capacity);
        if (grown == NULL) {
            response->overflowed = true;
            return false;
        }
        response->body = grown;
        response->capacity = capacity;
    }
    memcpy(response->body + response->length, data, data_length);
    response->length += data_length;
    response->body[response->length] = '\0';
    return true;
}

static esp_err_t long_term_memory_sync_http_event(
    esp_http_client_event_t *event
) {
    long_term_memory_sync_response_t *response =
        (long_term_memory_sync_response_t *)event->user_data;
    if (response == NULL || event->event_id != HTTP_EVENT_ON_DATA ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    // The callback stays silent on overflow so the transfer completes and the
    // caller can reject the payload with a single clear result.
    (void)long_term_memory_sync_response_append(
        response,
        (const char *)event->data,
        (size_t)event->data_len
    );
    return ESP_OK;
}

// Resolve the session token, device id and platform base url shared by the
// pull and the upload. The caller owns and must clear the token.
static esp_err_t long_term_memory_sync_prepare(
    char *session_token,
    size_t session_token_size,
    char *device_id,
    size_t device_id_size,
    char *base_url,
    size_t base_url_size
) {
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED ||
        !time_sync_is_synchronized() ||
        !device_binding_client_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_binding_client_copy_session_token(
            session_token,
            session_token_size
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_identity_copy(device_id, device_id_size) != ESP_OK) {
        memset(session_token, 0, session_token_size);
        return ESP_ERR_INVALID_STATE;
    }
    if (device_binding_client_get_platform_base_url(
            base_url,
            base_url_size
        ) != ESP_OK ||
        strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, session_token_size);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// Translate the response status shared by both verbs. A 401/403 drops the
// local token so the next cycle performs a fresh challenge instead of
// retrying a revoked credential.
static esp_err_t long_term_memory_sync_classify_status(
    esp_err_t transport_result,
    int status_code
) {
    if (transport_result != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t long_term_memory_sync_request(
    const char *url,
    esp_http_client_method_t method,
    const char *session_token,
    const char *request_body,
    int request_body_length,
    long_term_memory_sync_response_t *response
) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = method;
    config.timeout_ms = 15000;
    config.event_handler = long_term_memory_sync_http_event;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char authorization[DEVICE_BINDING_SESSION_TOKEN_SIZE + 16] = {0};
    snprintf(
        authorization,
        sizeof(authorization),
        "Bearer %s",
        session_token
    );
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Accept", "application/json");
    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_header(
            client,
            "Content-Type",
            "application/json"
        );
        esp_http_client_set_post_field(
            client,
            request_body,
            request_body_length
        );
    }
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    return long_term_memory_sync_classify_status(
        transport_result,
        status_code
    );
}

static bool long_term_memory_sync_memories_url(
    char *url,
    size_t url_size,
    const char *base_url,
    const char *device_id
) {
    const int written = snprintf(
        url,
        url_size,
        "%s/api/v1/devices/%s/memories",
        base_url,
        device_id
    );
    return written > 0 && (size_t)written < url_size;
}

// Copy one JSON string field into a fixed-size remote record. Returns false
// when the field is missing or would not fit, so the entry is dropped rather
// than truncated into a different fact.
static bool long_term_memory_sync_copy_field(
    char *destination,
    size_t destination_size,
    const char *value
) {
    if (destination_size == 0 || value == NULL || value[0] == '\0') {
        return false;
    }
    const size_t length = strlen(value);
    if (length >= destination_size) {
        return false;
    }
    memcpy(destination, value, length + 1);
    return true;
}

static esp_err_t long_term_memory_sync_merge(
    long_term_memory_entry_t *local,
    size_t local_count,
    const char *child_id,
    const long_term_memory_sync_remote_t *remote,
    size_t remote_count
) {
    for (size_t index = 0; index < remote_count; ++index) {
        const long_term_memory_sync_remote_t *entry = &remote[index];
        // A key the device already holds is never overwritten: the local fact
        // may be newer than the server copy and would otherwise be reverted on
        // every pull.
        if (long_term_memory_sync_holds_key(
                local,
                local_count,
                child_id,
                entry->memory_key
            )) {
            continue;
        }
        long_term_memory_entry_t stored = {};
        if (!long_term_memory_sync_copy_field(
                stored.child_id,
                sizeof(stored.child_id),
                child_id
            ) ||
            !long_term_memory_sync_copy_field(
                stored.key,
                sizeof(stored.key),
                entry->memory_key
            ) ||
            !long_term_memory_sync_copy_field(
                stored.value,
                sizeof(stored.value),
                entry->memory_value
            )) {
            continue;
        }
        // The local entry id must fit LONG_TERM_MEMORY_ID_SIZE (40), so it is
        // derived from the key only when the key is short enough; otherwise
        // the entry is still stored and reachable by key, with an empty id
        // the store rejects. Keys are therefore gated before this point.
        if (!long_term_memory_sync_copy_field(
                stored.entry_id,
                sizeof(stored.entry_id),
                entry->memory_key
            )) {
            continue;
        }
        stored.updated_at_epoch_seconds = entry->updated_at_epoch_seconds;
        if (long_term_memory_remember(&stored) != ESP_OK) {
            // A full store cannot hold the rest of this snapshot, so the pull
            // stops here and the revision is not advanced. The next cycle
            // retries the same window once local entries are forgotten.
            ESP_LOGW(
                TAG,
                "merge stopped: local store full after %u entries",
                (unsigned)index
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (local_count < LONG_TERM_MEMORY_CAPACITY) {
            local[local_count] = stored;
            local_count += 1;
        }
    }
    return ESP_OK;
}

// Pull `since_revision` and merge the delta. Any failure leaves both the local
// store and the persisted revision untouched so the next cycle retries the
// same window.
static esp_err_t long_term_memory_sync_pull(
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    long_term_memory_sync_store_t current;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    current = s_store;
    xSemaphoreGive(s_lock);

    char url[LONG_TERM_MEMORY_SYNC_URL_SIZE] = {0};
    // Filled from the server response below; the device id is not the memory
    // owner, so it must never be used as the local child key.
    char child_id[LONG_TERM_MEMORY_CHILD_ID_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/memories?since_revision=%lld",
        base_url,
        device_id,
        (long long)(current.state.has_snapshot ? current.state.revision : 0)
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
#if LONG_TERM_MEMORY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    long_term_memory_sync_response_t response = {};
    const esp_err_t request_result = long_term_memory_sync_request(
        url,
        HTTP_METHOD_GET,
        session_token,
        NULL,
        0,
        &response
    );
    if (request_result != ESP_OK) {
        long_term_memory_sync_response_destroy(&response);
        return request_result;
    }
    if (response.overflowed || response.body == NULL) {
        long_term_memory_sync_response_destroy(&response);
        return ESP_ERR_INVALID_SIZE;
    }
    JsonDocument document;
    const bool is_parsed =
        deserializeJson(document, response.body) == DeserializationError::Ok;
    long_term_memory_sync_response_destroy(&response);
    if (!is_parsed) {
        return ESP_ERR_INVALID_ARG;
    }

    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonArrayConst memories = data["memories"].as<JsonArrayConst>();
    if (memories.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t response_revision = (int64_t)(data["revision"] | -1LL);
    if (response_revision < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // The server resolves which child this device serves and returns it. The
    // device must store memories under that id rather than under its own id,
    // otherwise the same child on two devices would never share a fact and
    // the whole point of syncing would be lost.
    const char *response_child_id = data["child_id"] | "";
    if (response_child_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!long_term_memory_sync_copy_field(
            child_id,
            sizeof(child_id),
            response_child_id
        )) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t entry_count = 0;
    for (JsonVariantConst value : memories) {
        (void)value;
        entry_count += 1;
    }
    // A delta larger than the local capacity is a full snapshot the device
    // cannot represent; applying part of it while advancing the revision would
    // silently drop memories forever.
    if (entry_count > LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *keys[LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY] = {};
    const char *values[LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY] = {};
    int32_t confidences[LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY] = {};
    int64_t revisions[LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY] = {};
    int64_t updated_at[LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY] = {};
    size_t parsed = 0;
    for (JsonVariantConst value : memories) {
        if (parsed >= LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY) {
            break;
        }
        keys[parsed] = value["memory_key"] | "";
        values[parsed] = value["memory_value"] | "";
        confidences[parsed] = value["confidence"] | 0;
        revisions[parsed] = (int64_t)(value["revision"] | 0LL);
        updated_at[parsed] = (int64_t)(value["updated_at"] | 0LL);
        parsed += 1;
    }
    long_term_memory_sync_remote_t validated[
        LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY
    ] = {};
    size_t validated_count = 0;
    // The core owns the duplicate, bounds and revision rules so the host test
    // exercises exactly the decisions the device makes.
    if (!long_term_memory_sync_parse_remote(
            validated,
            LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY,
            &validated_count,
            parsed,
            parsed > 0 ? keys : NULL,
            parsed > 0 ? values : NULL,
            parsed > 0 ? confidences : NULL,
            parsed > 0 ? revisions : NULL,
            parsed > 0 ? updated_at : NULL,
            response_revision,
            current.state.revision,
            current.state.has_snapshot
        )) {
        return ESP_ERR_INVALID_ARG;
    }

    // Revocation clears the local store, so merging would write back facts the
    // guardian revoked. The revision stays put and the next authorized pull
    // re-applies the full snapshot.
    if (!long_term_memory_is_authorized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (entry_count == 0) {
        // An empty snapshot is authoritative: the guardian deleted every
        // memory (or the account holds none), so the local store is cleared
        // for every child. A failed clear keeps the revision unadvanced.
        size_t removed = 0;
        const esp_err_t forget_result = long_term_memory_forget_child(
            child_id,
            &removed
        );
        if (forget_result != ESP_OK) {
            return forget_result;
        }
        long_term_memory_entry_t remaining[LONG_TERM_MEMORY_CAPACITY] = {};
        size_t remaining_count = 0;
        if (long_term_memory_list(
                remaining,
                LONG_TERM_MEMORY_CAPACITY,
                &remaining_count
            ) == ESP_OK &&
            remaining_count > 0) {
            // Entries for a previously bound device id would otherwise linger
            // after an identity change, so the snapshot clears them too.
            for (size_t index = 0; index < remaining_count; ++index) {
                (void)long_term_memory_forget(remaining[index].entry_id);
            }
        }
    }
    long_term_memory_entry_t local[LONG_TERM_MEMORY_CAPACITY] = {};
    size_t local_count = 0;
    const esp_err_t list_result = long_term_memory_list(
        local,
        LONG_TERM_MEMORY_CAPACITY,
        &local_count
    );
    if (list_result != ESP_OK) {
        return list_result;
    }
    const esp_err_t merge_result = long_term_memory_sync_merge(
        local,
        local_count,
        child_id,
        validated,
        validated_count
    );
    if (merge_result != ESP_OK) {
        return merge_result;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    long_term_memory_sync_store_t updated = s_store;
    updated.state.revision = response_revision;
    updated.state.has_snapshot = true;
    // The cursor advances only after every entry was merged, so a restart
    // never asks the server to skip a window the store never received.
    const esp_err_t save_result = long_term_memory_sync_store_save(&updated);
    if (save_result == ESP_OK) {
        s_store = updated;
        s_last_success_epoch_seconds = time_sync_get_last_synced_epoch();
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(
        TAG,
        "pull applied %u memories at revision %lld",
        (unsigned)validated_count,
        (long long)response_revision
    );
    return save_result;
}

static bool long_term_memory_sync_key_is_valid(const char *memory_key) {
    if (memory_key == NULL || memory_key[0] == '\0') {
        return false;
    }
    const size_t length = strlen(memory_key);
    if (length > LONG_TERM_MEMORY_SYNC_KEY_MAX_LENGTH) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char character = (unsigned char)memory_key[index];
        if (isspace(character) || iscntrl(character)) {
            return false;
        }
    }
    return true;
}

// Report the next pending batch in one request. Only a batch the platform
// accepted in full leaves the pending list, so nothing is marked synced before
// the server confirmed it.
static esp_err_t long_term_memory_sync_push(
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    long_term_memory_sync_store_t current;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    current = s_store;
    xSemaphoreGive(s_lock);
    if (current.pending_count == 0) {
        return ESP_OK;
    }
    if (!long_term_memory_is_authorized()) {
        return ESP_ERR_INVALID_STATE;
    }

    long_term_memory_entry_t local[LONG_TERM_MEMORY_CAPACITY] = {};
    size_t local_count = 0;
    const esp_err_t list_result = long_term_memory_list(
        local,
        LONG_TERM_MEMORY_CAPACITY,
        &local_count
    );
    if (list_result != ESP_OK) {
        return list_result;
    }

    const size_t batch_count =
        current.pending_count < LONG_TERM_MEMORY_SYNC_BATCH_CAPACITY
        ? current.pending_count
        : LONG_TERM_MEMORY_SYNC_BATCH_CAPACITY;
    char batch[LONG_TERM_MEMORY_SYNC_BATCH_CAPACITY][LONG_TERM_MEMORY_KEY_SIZE] =
        {};
    memcpy(batch, current.pending_keys, batch_count * sizeof(batch[0]));

    JsonDocument payload;
    JsonArray memories = payload["memories"].to<JsonArray>();
    // Only keys that still map to a local entry are sent; a key whose entry was
    // forgotten is dropped from the queue below instead of retried forever.
    bool is_included[LONG_TERM_MEMORY_SYNC_BATCH_CAPACITY] = {};
    size_t included_count = 0;
    for (size_t index = 0; index < batch_count; ++index) {
        if (!long_term_memory_sync_key_is_valid(batch[index])) {
            continue;
        }
        const long_term_memory_entry_t *entry = NULL;
        for (size_t cursor = 0; cursor < local_count; ++cursor) {
            if (strcmp(local[cursor].key, batch[index]) == 0) {
                entry = &local[cursor];
                break;
            }
        }
        if (entry == NULL) {
            continue;
        }
        JsonObject item = memories.add<JsonObject>();
        item["memory_key"] = batch[index];
        // The value leaves the device only through this authenticated request
        // and is never written to a log line.
        item["memory_value"] = entry->value;
        item["confidence"] = LONG_TERM_MEMORY_SYNC_UPLOAD_CONFIDENCE;
        is_included[index] = true;
        included_count += 1;
    }

    if (included_count == 0) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        long_term_memory_sync_store_t pruned = s_store;
        size_t retained = 0;
        for (size_t index = 0; index < s_store.pending_count; ++index) {
            const char *key = s_store.pending_keys[index];
            bool is_batch_key = false;
            for (size_t cursor = 0; cursor < batch_count; ++cursor) {
                if (strcmp(key, batch[cursor]) == 0) {
                    is_batch_key = true;
                    break;
                }
            }
            if (is_batch_key) {
                continue;
            }
            memcpy(
                pruned.pending_keys[retained],
                s_store.pending_keys[index],
                LONG_TERM_MEMORY_KEY_SIZE
            );
            retained += 1;
        }
        pruned.pending_count = retained;
        if (long_term_memory_sync_store_save(&pruned) == ESP_OK) {
            s_store = pruned;
        }
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }

    char request_body[LONG_TERM_MEMORY_SYNC_REQUEST_SIZE] = {};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_SIZE;
    }
    char url[LONG_TERM_MEMORY_SYNC_URL_SIZE] = {0};
    if (!long_term_memory_sync_memories_url(
            url,
            sizeof(url),
            base_url,
            device_id
        )) {
        return ESP_ERR_INVALID_ARG;
    }
#if LONG_TERM_MEMORY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    long_term_memory_sync_response_t response = {};
    const esp_err_t request_result = long_term_memory_sync_request(
        url,
        HTTP_METHOD_POST,
        session_token,
        request_body,
        (int)body_length,
        &response
    );
    memset(request_body, 0, sizeof(request_body));
    if (request_result != ESP_OK) {
        long_term_memory_sync_response_destroy(&response);
        return request_result;
    }
    JsonDocument document;
    const bool is_parsed = response.body != NULL && !response.overflowed &&
        deserializeJson(document, response.body) == DeserializationError::Ok;
    long_term_memory_sync_response_destroy(&response);
    if (!is_parsed) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t accepted = (int64_t)(data["accepted"] | -1LL);
    const int64_t server_revision = (int64_t)(data["revision"] | -1LL);
    if (accepted < 0 || server_revision < 0 ||
        (size_t)accepted > included_count) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    long_term_memory_sync_store_t updated = s_store;
    const bool is_fully_accepted = (size_t)accepted == included_count;
    // A partial acceptance leaves the cursor alone: the rejected entries have
    // no revision the device can skip over without losing them.
    if (is_fully_accepted) {
        updated.state.revision = server_revision > updated.state.revision
            ? server_revision
            : updated.state.revision;
        updated.state.has_snapshot = true;
    }
    size_t retained = 0;
    for (size_t index = 0; index < s_store.pending_count; ++index) {
        bool is_batch_key = false;
        for (size_t cursor = 0; cursor < batch_count; ++cursor) {
            if (is_included[cursor] &&
                strcmp(s_store.pending_keys[index], batch[cursor]) == 0) {
                is_batch_key = true;
                break;
            }
        }
        // A batch key is cleared only once the whole batch is acknowledged;
        // otherwise it stays queued and the server deduplicates by key.
        if (is_batch_key && is_fully_accepted) {
            continue;
        }
        memcpy(
            updated.pending_keys[retained],
            s_store.pending_keys[index],
            LONG_TERM_MEMORY_KEY_SIZE
        );
        retained += 1;
    }
    updated.pending_count = retained;
    const esp_err_t save_result = long_term_memory_sync_store_save(&updated);
    if (save_result == ESP_OK) {
        s_store = updated;
        s_last_success_epoch_seconds = time_sync_get_last_synced_epoch();
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(
        TAG,
        "upload accepted %lld of %u memories",
        (long long)accepted,
        (unsigned)included_count
    );
    return save_result;
}

esp_err_t long_term_memory_sync_enqueue_key(const char *memory_key) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!long_term_memory_sync_key_is_valid(memory_key)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    long_term_memory_sync_store_t updated = s_store;
    const bool is_added = long_term_memory_sync_pending_add(
        updated.pending_keys,
        LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY,
        &updated.pending_count,
        memory_key
    );
    esp_err_t result = ESP_OK;
    if (!is_added) {
        // The entry stays local; the caller retries after the queue drains.
        result = ESP_ERR_NO_MEM;
    } else if (long_term_memory_sync_store_save(&updated) != ESP_OK) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_store = updated;
    }
    xSemaphoreGive(s_lock);
    return result;
}

// Create the state lock on first use. Status and consent transitions must be
// observable from boot, before the worker module is initialized.
static bool long_term_memory_sync_ensure_lock(void) {
    if (s_lock != NULL) {
        return true;
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

esp_err_t long_term_memory_sync_get_status(
    long_term_memory_sync_status_snapshot_t *status_out
) {
    if (status_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status_out, 0, sizeof(*status_out));
    // Status is readable before the worker starts so a UI can show "not yet
    // synced" during boot instead of failing the call.
    if (!long_term_memory_sync_ensure_lock()) {
        status_out->status = LONG_TERM_MEMORY_SYNC_STATUS_IDLE;
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    status_out->status = s_status;
    status_out->revision = s_store.state.revision;
    status_out->has_snapshot = s_store.state.has_snapshot;
    status_out->pending_count = s_store.pending_count;
    status_out->last_success_epoch_seconds = s_last_success_epoch_seconds;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static esp_err_t long_term_memory_sync_run(void) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[LONG_TERM_MEMORY_SYNC_URL_SIZE] = {0};
    const esp_err_t prepare_result = long_term_memory_sync_prepare(
        session_token,
        sizeof(session_token),
        device_id,
        sizeof(device_id),
        base_url,
        sizeof(base_url)
    );
    if (prepare_result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return prepare_result;
    }
    // Pull first, then upload: a memory learned offline is pushed with the
    // revision the server assigned to every earlier entry, so the two sides
    // converge in one cycle instead of two.
    const esp_err_t pull_result = long_term_memory_sync_pull(
        session_token,
        device_id,
        base_url
    );
    esp_err_t push_result = ESP_OK;
    if (pull_result == ESP_OK) {
        push_result = long_term_memory_sync_push(
            session_token,
            device_id,
            base_url
        );
    }
    memset(session_token, 0, sizeof(session_token));
    return pull_result != ESP_OK ? pull_result : push_result;
}

esp_err_t long_term_memory_sync_now(void) {
    if (!long_term_memory_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    // Consent is checked before the worker state because a revoked or absent
    // authorization is the operative reason, and it is the state a guardian
    // controller must be able to observe even before the worker starts.
    if (!long_term_memory_is_authorized()) {
        // Without guardian consent there is nothing legitimate to send or
        // merge; reporting an unavailable state keeps the worker on its retry
        // interval without opening a connection.
        if (long_term_memory_sync_ensure_lock()) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status = LONG_TERM_MEMORY_SYNC_STATUS_UNAUTHORIZED;
            xSemaphoreGive(s_lock);
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_ready || !long_term_memory_sync_ensure_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status = LONG_TERM_MEMORY_SYNC_STATUS_SYNCING;
    xSemaphoreGive(s_lock);
    const esp_err_t result = long_term_memory_sync_run();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (result == ESP_OK) {
        s_status = LONG_TERM_MEMORY_SYNC_STATUS_IDLE;
    } else if (result == ESP_ERR_INVALID_STATE) {
        s_status = LONG_TERM_MEMORY_SYNC_STATUS_OFFLINE;
    } else {
        s_status = LONG_TERM_MEMORY_SYNC_STATUS_FAILED;
    }
    xSemaphoreGive(s_lock);
    return result;
}

static void long_term_memory_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = long_term_memory_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                // Only the status name is logged; a memory value must never
                // reach the log sink.
                ESP_LOGW(
                    TAG,
                    "memory sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_LONG_TERM_MEMORY_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_LONG_TERM_MEMORY_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t long_term_memory_sync_initialize(void) {
    if (!long_term_memory_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    if (!long_term_memory_sync_ensure_lock()) {
        return ESP_ERR_NO_MEM;
    }
    long_term_memory_sync_store_t loaded;
    // A missing or corrupt record resets the cursor and forces a full pull;
    // that is recoverable, so it must not fail initialization.
    (void)long_term_memory_sync_store_load(&loaded);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_store = loaded;
    s_status = long_term_memory_is_authorized()
        ? LONG_TERM_MEMORY_SYNC_STATUS_IDLE
        : LONG_TERM_MEMORY_SYNC_STATUS_UNAUTHORIZED;
    xSemaphoreGive(s_lock);
    s_stop = false;
    if (xTaskCreate(
            long_term_memory_sync_worker,
            "ltm_sync",
            6144,
            NULL,
            2,
            &s_task_handle
        ) != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

const module_descriptor_t *long_term_memory_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "long_term_memory_sync",
        .version = "1.0.0",
        .initialize = long_term_memory_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef LONG_TERM_MEMORY_SYNC_HOST_TESTS
extern "C" {

bool long_term_memory_sync_host_is_ready(void) {
    return s_ready;
}

void long_term_memory_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
    memset(&s_store, 0, sizeof(s_store));
    s_status = LONG_TERM_MEMORY_SYNC_STATUS_IDLE;
    s_last_success_epoch_seconds = 0;
}

void long_term_memory_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

bool long_term_memory_sync_host_is_pending(const char *memory_key) {
    bool is_pending = false;
    if (s_lock == NULL || memory_key == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t index = 0; index < s_store.pending_count; ++index) {
        if (strcmp(s_store.pending_keys[index], memory_key) == 0) {
            is_pending = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return is_pending;
}

}  // extern "C"
#endif
