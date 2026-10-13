#include "playback_request.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "playback_request_core.h"
#include "playback_request_sync_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The bounded playback request cache persists as one binary blob so the device
// keeps the guardian's on-demand requests across restarts without a dedicated
// partition.
#define PLAYBACK_REQUEST_STORE_KEY "playback_request_index"

static playback_request_core_t s_core;
static SemaphoreHandle_t s_lock;
static bool s_ready;

// Every read and write goes through the lock so the sync worker and the
// playback loop never observe a half-updated request cache.
#define PLAYBACK_REQUEST_LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define PLAYBACK_REQUEST_UNLOCK() xSemaphoreGive(s_lock)

static esp_err_t playback_request_persist_core(
    const playback_request_core_t *core
) {
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t stored = config_store_set_blob(
        PLAYBACK_REQUEST_STORE_KEY,
        core,
        sizeof(*core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void playback_request_restore(void) {
    playback_request_core_t restored;
    memset(&restored, 0, sizeof(restored));
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        PLAYBACK_REQUEST_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= PLAYBACK_REQUEST_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid cache so a
    // corrupt store never blocks startup.
    playback_request_core_init(&s_core);
}

esp_err_t playback_request_init(void) {
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    playback_request_core_init(&s_core);
    playback_request_restore();
    s_ready = true;
    return ESP_OK;
}

bool playback_request_is_ready(void) {
    return s_ready;
}

esp_err_t playback_request_upsert(const playback_request_t *request) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    PLAYBACK_REQUEST_LOCK();
    playback_request_core_t updated = s_core;
    const playback_request_error_t result = playback_request_core_upsert(
        &updated,
        request
    );
    if (result != PLAYBACK_REQUEST_OK) {
        PLAYBACK_REQUEST_UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = playback_request_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    PLAYBACK_REQUEST_UNLOCK();
    return persisted;
}

esp_err_t playback_request_remove(const char *request_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    PLAYBACK_REQUEST_LOCK();
    playback_request_core_t updated = s_core;
    const playback_request_error_t result = playback_request_core_remove(
        &updated,
        request_id
    );
    if (result != PLAYBACK_REQUEST_OK) {
        PLAYBACK_REQUEST_UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = playback_request_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    PLAYBACK_REQUEST_UNLOCK();
    return persisted;
}

esp_err_t playback_request_merge_remote(
    const playback_request_t *remote,
    size_t remote_count
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    PLAYBACK_REQUEST_LOCK();
    playback_request_core_t updated = s_core;
    const playback_request_error_t result = playback_request_sync_core_merge(
        &updated,
        remote,
        remote_count
    );
    if (result != PLAYBACK_REQUEST_OK) {
        PLAYBACK_REQUEST_UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = playback_request_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    PLAYBACK_REQUEST_UNLOCK();
    return persisted;
}

esp_err_t playback_request_acknowledge(const char *request_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    PLAYBACK_REQUEST_LOCK();
    playback_request_core_t updated = s_core;
    const playback_request_error_t result =
        playback_request_core_acknowledge(&updated, request_id);
    switch (result) {
        case PLAYBACK_REQUEST_OK:
            break;
        case PLAYBACK_REQUEST_ERR_NOT_FOUND:
            PLAYBACK_REQUEST_UNLOCK();
            return ESP_ERR_NOT_FOUND;
        case PLAYBACK_REQUEST_ERR_INVALID_STATE:
            PLAYBACK_REQUEST_UNLOCK();
            return ESP_ERR_INVALID_STATE;
        default:
            PLAYBACK_REQUEST_UNLOCK();
            return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = playback_request_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    PLAYBACK_REQUEST_UNLOCK();
    return persisted;
}

esp_err_t playback_request_complete(
    const char *request_id,
    playback_request_outcome_t outcome
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    PLAYBACK_REQUEST_LOCK();
    playback_request_core_t updated = s_core;
    const playback_request_error_t result = playback_request_core_complete(
        &updated,
        request_id,
        outcome
    );
    switch (result) {
        case PLAYBACK_REQUEST_OK:
            break;
        case PLAYBACK_REQUEST_ERR_NOT_FOUND:
            PLAYBACK_REQUEST_UNLOCK();
            return ESP_ERR_NOT_FOUND;
        case PLAYBACK_REQUEST_ERR_INVALID_STATE:
            PLAYBACK_REQUEST_UNLOCK();
            return ESP_ERR_INVALID_STATE;
        default:
            PLAYBACK_REQUEST_UNLOCK();
            return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = playback_request_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    PLAYBACK_REQUEST_UNLOCK();
    return persisted;
}

esp_err_t playback_request_list(
    playback_request_t *requests_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (requests_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    PLAYBACK_REQUEST_LOCK();
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        requests_out[index] = s_core.requests[index];
    }
    *count_out = s_core.count;
    PLAYBACK_REQUEST_UNLOCK();
    return ESP_OK;
}

esp_err_t playback_request_list_pending(
    playback_request_t *requests_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (requests_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    PLAYBACK_REQUEST_LOCK();
    // The cache is newest-first, so walk it backwards for oldest-first order.
    size_t matched = 0;
    for (size_t offset = 0; offset < s_core.count; ++offset) {
        const size_t index = s_core.count - 1 - offset;
        const playback_request_t *request = &s_core.requests[index];
        if (playback_request_status_is_terminal(request->status)) {
            continue;
        }
        if (matched < capacity) {
            requests_out[matched] = *request;
        }
        matched += 1;
    }
    *count_out = matched;
    PLAYBACK_REQUEST_UNLOCK();
    return ESP_OK;
}

bool playback_request_get(
    const char *request_id,
    playback_request_t *request_out
) {
    if (!s_ready || request_id == NULL || request_out == NULL) {
        return false;
    }
    PLAYBACK_REQUEST_LOCK();
    const size_t index = playback_request_core_find(&s_core, request_id);
    if (index >= s_core.count) {
        PLAYBACK_REQUEST_UNLOCK();
        return false;
    }
    *request_out = s_core.requests[index];
    PLAYBACK_REQUEST_UNLOCK();
    return true;
}

size_t playback_request_count(void) {
    if (!s_ready) {
        return 0;
    }
    PLAYBACK_REQUEST_LOCK();
    const size_t count = s_core.count;
    PLAYBACK_REQUEST_UNLOCK();
    return count;
}

const char *playback_request_status_name(playback_request_status_t status) {
    switch (status) {
        case PLAYBACK_REQUEST_PENDING:
            return "pending";
        case PLAYBACK_REQUEST_ACKNOWLEDGED:
            return "acknowledged";
        case PLAYBACK_REQUEST_COMPLETED:
            return "completed";
        case PLAYBACK_REQUEST_FAILED:
            return "failed";
        case PLAYBACK_REQUEST_CANCELLED:
            return "cancelled";
        default:
            return "unknown";
    }
}

bool playback_request_status_from_name(
    const char *name,
    playback_request_status_t *status_out
) {
    if (name == NULL) {
        return false;
    }
    playback_request_status_t parsed;
    if (strcmp(name, "pending") == 0) {
        parsed = PLAYBACK_REQUEST_PENDING;
    } else if (strcmp(name, "acknowledged") == 0) {
        parsed = PLAYBACK_REQUEST_ACKNOWLEDGED;
    } else if (strcmp(name, "completed") == 0) {
        parsed = PLAYBACK_REQUEST_COMPLETED;
    } else if (strcmp(name, "failed") == 0) {
        parsed = PLAYBACK_REQUEST_FAILED;
    } else if (strcmp(name, "cancelled") == 0) {
        parsed = PLAYBACK_REQUEST_CANCELLED;
    } else {
        return false;
    }
    if (status_out != NULL) {
        *status_out = parsed;
    }
    return true;
}

const char *playback_request_outcome_name(playback_request_outcome_t outcome) {
    switch (outcome) {
        case PLAYBACK_REQUEST_OUTCOME_NONE:
            return "none";
        case PLAYBACK_REQUEST_OUTCOME_PLAYED:
            return "played";
        case PLAYBACK_REQUEST_OUTCOME_SKIPPED:
            return "skipped";
        case PLAYBACK_REQUEST_OUTCOME_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

bool playback_request_outcome_from_name(
    const char *name,
    playback_request_outcome_t *outcome_out
) {
    if (name == NULL) {
        return false;
    }
    playback_request_outcome_t parsed;
    if (strcmp(name, "none") == 0) {
        parsed = PLAYBACK_REQUEST_OUTCOME_NONE;
    } else if (strcmp(name, "played") == 0) {
        parsed = PLAYBACK_REQUEST_OUTCOME_PLAYED;
    } else if (strcmp(name, "skipped") == 0) {
        parsed = PLAYBACK_REQUEST_OUTCOME_SKIPPED;
    } else if (strcmp(name, "error") == 0) {
        parsed = PLAYBACK_REQUEST_OUTCOME_ERROR;
    } else {
        return false;
    }
    if (outcome_out != NULL) {
        *outcome_out = parsed;
    }
    return true;
}

const char *playback_request_error_name(playback_request_error_t error) {
    switch (error) {
        case PLAYBACK_REQUEST_OK:
            return "ok";
        case PLAYBACK_REQUEST_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case PLAYBACK_REQUEST_ERR_FULL:
            return "full";
        case PLAYBACK_REQUEST_ERR_NOT_FOUND:
            return "not_found";
        case PLAYBACK_REQUEST_ERR_STORAGE:
            return "storage";
        case PLAYBACK_REQUEST_ERR_INVALID_STATE:
            return "invalid_state";
        default:
            return "unknown";
    }
}

static esp_err_t playback_request_initialize(void) {
    return playback_request_init();
}

const module_descriptor_t *playback_request_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "playback_request",
        .version = "1.0.0",
        .initialize = playback_request_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
