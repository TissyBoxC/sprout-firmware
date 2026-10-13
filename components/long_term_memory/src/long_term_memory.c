#include "long_term_memory.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "long_term_memory_core.h"

// Long-term memory persists as one binary blob so a device keeps the
// guardian-authorized facts across restarts without a dedicated partition.
#define LONG_TERM_MEMORY_STORE_KEY "ltm_index"
CONFIG_STORE_KEY_ASSERT(LONG_TERM_MEMORY_STORE_KEY);

/** @brief Persisted envelope: the authorization flag plus the bounded store. */
typedef struct {
    uint32_t version;
    bool authorized;
    long_term_memory_core_t core;
} long_term_memory_blob_t;

#define LONG_TERM_MEMORY_BLOB_VERSION 1u

static long_term_memory_blob_t s_blob;
static bool s_ready;

static esp_err_t long_term_memory_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        LONG_TERM_MEMORY_STORE_KEY,
        &s_blob,
        sizeof(s_blob)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void long_term_memory_restore(void) {
    long_term_memory_blob_t restored;
    memset(&restored, 0, sizeof(restored));
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        LONG_TERM_MEMORY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.version == LONG_TERM_MEMORY_BLOB_VERSION &&
        restored.core.count <= LONG_TERM_MEMORY_CAPACITY) {
        s_blob = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, unauthorized store so
    // a corrupt store never blocks startup and never fabricates consent.
    memset(&s_blob, 0, sizeof(s_blob));
    s_blob.version = LONG_TERM_MEMORY_BLOB_VERSION;
    long_term_memory_core_init(&s_blob.core);
}

esp_err_t long_term_memory_init(void) {
    long_term_memory_restore();
    s_ready = true;
    return ESP_OK;
}

bool long_term_memory_is_ready(void) {
    return s_ready;
}

esp_err_t long_term_memory_set_authorized(bool authorized) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_blob.authorized = authorized;
    if (!authorized) {
        // Revoking consent deletes every remembered fact so an unauthorized
        // memory can never be read back.
        long_term_memory_core_clear(&s_blob.core);
    }
    return long_term_memory_persist();
}

bool long_term_memory_is_authorized(void) {
    return s_ready && s_blob.authorized;
}

esp_err_t long_term_memory_remember(const long_term_memory_entry_t *entry) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_blob.authorized) {
        return ESP_ERR_INVALID_STATE;
    }
    const long_term_memory_error_t result = long_term_memory_core_remember(
        &s_blob.core,
        entry
    );
    if (result == LONG_TERM_MEMORY_ERR_FULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (result != LONG_TERM_MEMORY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return long_term_memory_persist();
}

esp_err_t long_term_memory_forget(const char *entry_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const long_term_memory_error_t result = long_term_memory_core_forget(
        &s_blob.core,
        entry_id
    );
    if (result != LONG_TERM_MEMORY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return long_term_memory_persist();
}

esp_err_t long_term_memory_forget_child(
    const char *child_id,
    size_t *removed_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const long_term_memory_error_t result =
        long_term_memory_core_forget_child(
            &s_blob.core,
            child_id,
            removed_out
        );
    if (result != LONG_TERM_MEMORY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return long_term_memory_persist();
}

bool long_term_memory_recall(
    const char *child_id,
    const char *key,
    long_term_memory_entry_t *entry_out
) {
    if (!s_ready || !s_blob.authorized) {
        return false;
    }
    if (child_id == NULL || key == NULL || entry_out == NULL) {
        return false;
    }
    const size_t index = long_term_memory_core_find(
        &s_blob.core,
        child_id,
        key
    );
    if (index >= s_blob.core.count) {
        return false;
    }
    *entry_out = s_blob.core.entries[index];
    return true;
}

esp_err_t long_term_memory_list(
    long_term_memory_entry_t *entries_out,
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
    // Without authorization the store surfaces nothing, even if a stale blob
    // somehow carried entries.
    if (!s_blob.authorized) {
        *count_out = 0;
        return ESP_OK;
    }
    const size_t copied = s_blob.core.count < capacity
        ? s_blob.core.count
        : capacity;
    for (size_t index = 0; index < copied; ++index) {
        entries_out[index] = s_blob.core.entries[index];
    }
    *count_out = s_blob.core.count;
    return ESP_OK;
}

size_t long_term_memory_count(void) {
    if (!s_ready || !s_blob.authorized) {
        return 0;
    }
    return s_blob.core.count;
}

const char *long_term_memory_error_name(long_term_memory_error_t error) {
    switch (error) {
        case LONG_TERM_MEMORY_OK:
            return "ok";
        case LONG_TERM_MEMORY_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case LONG_TERM_MEMORY_ERR_FULL:
            return "full";
        case LONG_TERM_MEMORY_ERR_NOT_FOUND:
            return "not_found";
        case LONG_TERM_MEMORY_ERR_STORAGE:
            return "storage";
        case LONG_TERM_MEMORY_ERR_CONSENT_REQUIRED:
            return "consent_required";
        default:
            return "unknown";
    }
}

static esp_err_t long_term_memory_initialize(void) {
    return long_term_memory_init();
}

const module_descriptor_t *long_term_memory_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "long_term_memory",
        .version = "1.0.0",
        .initialize = long_term_memory_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
