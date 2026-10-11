#include "favorites.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "favorites_core.h"

// The favourites index is persisted as one binary blob so the device keeps the
// guardian's bookmarks across restarts without a dedicated partition.
#define FAVORITES_STORE_KEY "favorites_index"

static favorites_core_t s_core;
static bool s_ready;

static esp_err_t favorites_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        FAVORITES_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void favorites_restore(void) {
    favorites_core_t restored = {0};
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        FAVORITES_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= FAVORITES_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid index so a
    // corrupt store never blocks startup.
    favorites_core_init(&s_core);
}

esp_err_t favorites_init(void) {
    favorites_core_init(&s_core);
    favorites_restore();
    s_ready = true;
    return ESP_OK;
}

bool favorites_is_ready(void) {
    return s_ready;
}

esp_err_t favorites_add(const char *package_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const favorites_error_t result = favorites_core_add(&s_core, package_id);
    if (result != FAVORITES_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return favorites_persist();
}

esp_err_t favorites_remove(const char *package_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const favorites_error_t result = favorites_core_remove(&s_core, package_id);
    if (result != FAVORITES_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return favorites_persist();
}

esp_err_t favorites_toggle(const char *package_id, bool *is_favorite_out) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const favorites_error_t result = favorites_core_toggle(
        &s_core,
        package_id,
        is_favorite_out
    );
    if (result != FAVORITES_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return favorites_persist();
}

bool favorites_contains(const char *package_id) {
    return favorites_core_contains(&s_core, package_id);
}

esp_err_t favorites_list(
    char (*package_ids_out)[CONTENT_LIBRARY_ID_SIZE],
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (package_ids_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        memcpy(
            package_ids_out[index],
            s_core.package_ids[index],
            CONTENT_LIBRARY_ID_SIZE
        );
    }
    *count_out = s_core.count;
    return ESP_OK;
}

size_t favorites_count(void) {
    return s_core.count;
}

const char *favorites_error_name(favorites_error_t error) {
    switch (error) {
        case FAVORITES_OK:
            return "ok";
        case FAVORITES_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case FAVORITES_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case FAVORITES_ERR_FULL:
            return "full";
        case FAVORITES_ERR_NOT_FOUND:
            return "not_found";
        case FAVORITES_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t favorites_initialize(void) {
    return favorites_init();
}

const module_descriptor_t *favorites_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "favorites",
        .version = "1.0.0",
        .initialize = favorites_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
