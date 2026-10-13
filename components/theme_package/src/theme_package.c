#include "theme_package.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "theme_package_core.h"
#include "theme_package_sync_core.h"

// The bounded theme cache persists as one binary blob so a device keeps its
// theme menu across restarts without a dedicated partition.
#define THEME_PACKAGE_STORE_KEY "theme_index"
CONFIG_STORE_KEY_ASSERT(THEME_PACKAGE_STORE_KEY);

static theme_package_core_t s_core;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static esp_err_t theme_package_persist_core(
    const theme_package_core_t *core
) {
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t stored = config_store_set_blob(
        THEME_PACKAGE_STORE_KEY,
        core,
        sizeof(*core)
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
    if (s_ready) {
        return ESP_OK;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
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
    xSemaphoreTake(s_lock, portMAX_DELAY);
    theme_package_core_t updated = s_core;
    const theme_package_error_t result = theme_package_core_upsert(
        &updated,
        pack
    );
    if (result != THEME_PACKAGE_OK) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = theme_package_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    xSemaphoreGive(s_lock);
    return persisted;
}

esp_err_t theme_package_remove(const char *theme_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    theme_package_core_t updated = s_core;
    const theme_package_error_t result = theme_package_core_remove(
        &updated,
        theme_id
    );
    if (result != THEME_PACKAGE_OK) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = theme_package_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    xSemaphoreGive(s_lock);
    return persisted;
}

esp_err_t theme_package_replace(
    const theme_pack_t *packs,
    size_t pack_count
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    theme_package_core_t updated = {0};
    const theme_package_error_t result = theme_package_sync_core_replace(
        &updated,
        packs,
        pack_count
    );
    if (result != THEME_PACKAGE_OK) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = theme_package_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    xSemaphoreGive(s_lock);
    return persisted;
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
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        packs_out[index] = s_core.packs[index];
    }
    *count_out = s_core.count;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool theme_package_get(const char *theme_id, theme_pack_t *pack_out) {
    if (!s_ready || theme_id == NULL || pack_out == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t index = theme_package_core_find(&s_core, theme_id);
    if (index >= s_core.count) {
        xSemaphoreGive(s_lock);
        return false;
    }
    *pack_out = s_core.packs[index];
    xSemaphoreGive(s_lock);
    return true;
}

bool theme_package_contains_package(
    const char *theme_id,
    const char *package_id,
    size_t *member_index_out
) {
    if (!s_ready || theme_id == NULL || package_id == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t pack_index = theme_package_core_find(&s_core, theme_id);
    if (pack_index >= s_core.count) {
        xSemaphoreGive(s_lock);
        return false;
    }
    const theme_pack_t *pack = &s_core.packs[pack_index];
    for (size_t index = 0; index < pack->member_count; ++index) {
        if (strcmp(pack->package_ids[index], package_id) == 0) {
            if (member_index_out != NULL) {
                *member_index_out = index;
            }
            xSemaphoreGive(s_lock);
            return true;
        }
    }
    xSemaphoreGive(s_lock);
    return false;
}

size_t theme_package_count(void) {
    if (!s_ready) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t count = s_core.count;
    xSemaphoreGive(s_lock);
    return count;
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
