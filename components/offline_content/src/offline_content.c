#include "offline_content.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "offline_content_core.h"

// The bounded offline content index persists as one binary blob so the device
// remembers which packages were pinned across restarts without a dedicated
// partition. Content bytes live in the content partition; only metadata here.
#define OFFLINE_CONTENT_STORE_KEY "offline_content_index"

static offline_content_core_t s_core;
static bool s_ready;

static esp_err_t offline_content_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        OFFLINE_CONTENT_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void offline_content_restore(void) {
    offline_content_core_t restored;
    memset(&restored, 0, sizeof(restored));
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        OFFLINE_CONTENT_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= OFFLINE_CONTENT_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid index so a
    // corrupt store never blocks startup.
    offline_content_core_init(&s_core);
}

esp_err_t offline_content_init(void) {
    offline_content_core_init(&s_core);
    offline_content_restore();
    s_ready = true;
    return ESP_OK;
}

bool offline_content_is_ready(void) {
    return s_ready;
}

esp_err_t offline_content_pin(const offline_content_entry_t *entry) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const offline_content_error_t result = offline_content_core_pin(
        &s_core,
        entry
    );
    switch (result) {
        case OFFLINE_CONTENT_OK:
            return offline_content_persist();
        case OFFLINE_CONTENT_ERR_FULL:
            return ESP_ERR_INVALID_SIZE;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t offline_content_mark_ready(const char *package_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const offline_content_error_t result = offline_content_core_mark_ready(
        &s_core,
        package_id
    );
    switch (result) {
        case OFFLINE_CONTENT_OK:
            return offline_content_persist();
        case OFFLINE_CONTENT_ERR_NOT_FOUND:
            return ESP_ERR_NOT_FOUND;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t offline_content_unpin(const char *package_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const offline_content_error_t result = offline_content_core_unpin(
        &s_core,
        package_id
    );
    if (result != OFFLINE_CONTENT_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return offline_content_persist();
}

bool offline_content_next_playable(offline_content_entry_t *entry_out) {
    if (!s_ready) {
        return false;
    }
    return offline_content_core_next_playable(&s_core, entry_out);
}

esp_err_t offline_content_list(
    offline_content_entry_t *entries_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (entries_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        entries_out[index] = s_core.entries[index];
    }
    *count_out = s_core.count;
    return ESP_OK;
}

bool offline_content_get(
    const char *package_id,
    offline_content_entry_t *entry_out
) {
    if (package_id == NULL || entry_out == NULL) {
        return false;
    }
    const size_t index = offline_content_core_find(&s_core, package_id);
    if (index >= s_core.count) {
        return false;
    }
    *entry_out = s_core.entries[index];
    return true;
}

size_t offline_content_count(void) {
    return s_core.count;
}

offline_content_snapshot_t offline_content_get_snapshot(void) {
    offline_content_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.pinned = s_core.count;
    for (size_t index = 0; index < s_core.count; ++index) {
        if (s_core.entries[index].local_ready) {
            snapshot.playable += 1;
        }
    }
    if (s_core.count > 0) {
        strncpy(
            snapshot.last_package_id,
            s_core.entries[0].package_id,
            OFFLINE_CONTENT_ID_SIZE - 1
        );
    }
    return snapshot;
}

const char *offline_content_error_name(offline_content_error_t error) {
    switch (error) {
        case OFFLINE_CONTENT_OK:
            return "ok";
        case OFFLINE_CONTENT_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case OFFLINE_CONTENT_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case OFFLINE_CONTENT_ERR_FULL:
            return "full";
        case OFFLINE_CONTENT_ERR_NOT_FOUND:
            return "not_found";
        case OFFLINE_CONTENT_ERR_STORAGE:
            return "storage";
        case OFFLINE_CONTENT_ERR_NOT_LOCAL:
            return "not_local";
        default:
            return "unknown";
    }
}

static esp_err_t offline_content_initialize(void) {
    return offline_content_init();
}

const module_descriptor_t *offline_content_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "offline_content",
        .version = "1.0.0",
        .initialize = offline_content_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
