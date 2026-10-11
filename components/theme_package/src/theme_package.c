#include "theme_package.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "theme_package_core.h"

// The bounded theme cache persists as one binary blob so a device keeps its
// theme menu across restarts without a dedicated partition.
#define THEME_PACKAGE_STORE_KEY "theme_package_index"

static theme_package_core_t s_core;
static bool s_ready;

static esp_err_t theme_package_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        THEME_PACKAGE_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void theme_package_restore(void) {
    theme_package_core_t restored = {0};
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        THEME_PACKAGE_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= THEME_PACKAGE_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid cache so a
    // corrupt store never blocks startup.
    theme_package_core_init(&s_core);
}

esp_err_t theme_package_init(void) {
    theme_package_core_init(&s_core);
    theme_package_restore();
    s_ready = true;
    return ESP_OK;
}

bool theme_package_is_ready(void) {
    return s_ready;
}

esp_err_t theme_package_upsert(const theme_pack_t *pack) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const theme_package_error_t result = theme_package_core_upsert(
        &s_core,
        pack
    );
    if (result != THEME_PACKAGE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return theme_package_persist();
}

esp_err_t theme_package_remove(const char *theme_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const theme_package_error_t result = theme_package_core_remove(
        &s_core,
        theme_id
    );
    if (result != THEME_PACKAGE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return theme_package_persist();
}

esp_err_t theme_package_list(
    theme_pack_t *packs_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (packs_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        packs_out[index] = s_core.packs[index];
    }
    *count_out = s_core.count;
    return ESP_OK;
}

bool theme_package_get(const char *theme_id, theme_pack_t *pack_out) {
    if (theme_id == NULL || pack_out == NULL) {
        return false;
    }
    const size_t index = theme_package_core_find(&s_core, theme_id);
    if (index >= s_core.count) {
        return false;
    }
    *pack_out = s_core.packs[index];
    return true;
}

bool theme_package_contains_package(
    const char *theme_id,
    const char *package_id,
    size_t *member_index_out
) {
    if (theme_id == NULL || package_id == NULL) {
        return false;
    }
    const size_t pack_index = theme_package_core_find(&s_core, theme_id);
    if (pack_index >= s_core.count) {
        return false;
    }
    const theme_pack_t *pack = &s_core.packs[pack_index];
    for (size_t index = 0; index < pack->member_count; ++index) {
        if (strcmp(pack->package_ids[index], package_id) == 0) {
            if (member_index_out != NULL) {
                *member_index_out = index;
            }
            return true;
        }
    }
    return false;
}

size_t theme_package_count(void) {
    return s_core.count;
}

const char *theme_package_error_name(theme_package_error_t error) {
    switch (error) {
        case THEME_PACKAGE_OK:
            return "ok";
        case THEME_PACKAGE_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case THEME_PACKAGE_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case THEME_PACKAGE_ERR_FULL:
            return "full";
        case THEME_PACKAGE_ERR_NOT_FOUND:
            return "not_found";
        case THEME_PACKAGE_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t theme_package_initialize(void) {
    return theme_package_init();
}

const module_descriptor_t *theme_package_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "theme_package",
        .version = "1.0.0",
        .initialize = theme_package_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
