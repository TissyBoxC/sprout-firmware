#include "ui_text.h"

#include <string.h>

#include "config_store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_text_store.h"

#ifndef CONFIG_UI_TEXT_ENTRY_CAPACITY
#define CONFIG_UI_TEXT_ENTRY_CAPACITY 128
#endif

#ifndef CONFIG_UI_TEXT_METADATA_KEY
#define CONFIG_UI_TEXT_METADATA_KEY "ui_text_index"
#endif

#define UI_TEXT_INDEX_MAGIC 0x53555431u

static const char *const TAG = "ui_text";

typedef struct {
    uint32_t magic;
    ui_text_package_t package;
    uint32_t entry_count;
    ui_text_entry_t entries[CONFIG_UI_TEXT_ENTRY_CAPACITY];
} ui_text_persisted_t;

// The active entries and the serialization buffer are ~50 KB each, which does
// not fit in internal DRAM beside the rest of the application. Both are
// allocated once from PSRAM (internal RAM as a fallback) so enabling remote UI
// text does not starve the Wi-Fi/audio stacks.
static ui_text_entry_t *ui_text_entries;
static ui_text_persisted_t *ui_text_persisted;
static ui_text_store_t ui_text_store;
static bool ui_text_ready;

static void *ui_text_alloc(size_t size) {
    return heap_caps_malloc_prefer(
        size,
        2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
}

static esp_err_t ui_text_persist(void) {
    memset(ui_text_persisted, 0, sizeof(*ui_text_persisted));
    ui_text_persisted->magic = UI_TEXT_INDEX_MAGIC;
    ui_text_persisted->package = ui_text_store.package;
    ui_text_persisted->entry_count = (uint32_t)ui_text_store.count;
    for (size_t index = 0; index < ui_text_store.count; ++index) {
        ui_text_persisted->entries[index] = ui_text_store.entries[index];
    }
    const esp_err_t result = config_store_set_blob(
        CONFIG_UI_TEXT_METADATA_KEY,
        ui_text_persisted,
        sizeof(*ui_text_persisted)
    );
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "package persistence failed: %s", esp_err_to_name(result));
    }
    return result;
}

static void ui_text_load(void) {
    memset(ui_text_persisted, 0, sizeof(*ui_text_persisted));
    size_t value_size = sizeof(*ui_text_persisted);
    const esp_err_t result = config_store_get_blob(
        CONFIG_UI_TEXT_METADATA_KEY,
        ui_text_persisted,
        &value_size
    );
    if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        return;
    }
    if (result != ESP_OK || value_size != sizeof(*ui_text_persisted) ||
        ui_text_persisted->magic != UI_TEXT_INDEX_MAGIC ||
        ui_text_persisted->entry_count > CONFIG_UI_TEXT_ENTRY_CAPACITY ||
        ui_text_persisted->entry_count == 0) {
        // A corrupt package must not prevent boot; clearing it falls back to
        // built-in text until the next successful sync.
        ESP_LOGW(TAG, "discarding corrupt ui text package");
        (void)config_store_erase_key(CONFIG_UI_TEXT_METADATA_KEY);
        return;
    }
    for (size_t index = 0; index < ui_text_persisted->entry_count; ++index) {
        if (!ui_text_store_entry_is_valid(&ui_text_persisted->entries[index])) {
            ESP_LOGW(TAG, "discarding invalid ui text entry");
            (void)config_store_erase_key(CONFIG_UI_TEXT_METADATA_KEY);
            return;
        }
    }
    bool changed = false;
    if (ui_text_store_apply_package(
            &ui_text_store,
            &ui_text_persisted->package,
            ui_text_persisted->entries,
            ui_text_persisted->entry_count,
            &changed
        ) != UI_TEXT_STORE_OK) {
        ESP_LOGW(TAG, "discarding unusable ui text package");
        (void)config_store_erase_key(CONFIG_UI_TEXT_METADATA_KEY);
    }
}

esp_err_t ui_text_init(void) {
    if (ui_text_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ui_text_entries == NULL) {
        ui_text_entries = (ui_text_entry_t *)ui_text_alloc(
            sizeof(ui_text_entry_t) * CONFIG_UI_TEXT_ENTRY_CAPACITY
        );
        if (ui_text_entries == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (ui_text_persisted == NULL) {
        ui_text_persisted = (ui_text_persisted_t *)ui_text_alloc(
            sizeof(ui_text_persisted_t)
        );
        if (ui_text_persisted == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (ui_text_store_init(
            &ui_text_store,
            ui_text_entries,
            CONFIG_UI_TEXT_ENTRY_CAPACITY
        ) != UI_TEXT_STORE_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    ui_text_load();
    ui_text_ready = true;
    return ESP_OK;
}

bool ui_text_is_ready(void) {
    return ui_text_ready;
}

bool ui_text_has_active_package(void) {
    return ui_text_ready && ui_text_store.active;
}

esp_err_t ui_text_active_package(ui_text_package_t *package_out) {
    if (!ui_text_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (package_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_text_store.active) {
        return ESP_ERR_NOT_FOUND;
    }
    *package_out = ui_text_store.package;
    return ESP_OK;
}

esp_err_t ui_text_apply_package(
    const ui_text_package_t *package,
    const ui_text_entry_t *entries,
    size_t entry_count
) {
    if (!ui_text_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    bool changed = false;
    const ui_text_store_result_t result = ui_text_store_apply_package(
        &ui_text_store,
        package,
        entries,
        entry_count,
        &changed
    );
    if (result == UI_TEXT_STORE_ERR_INVALID_ARGUMENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (result == UI_TEXT_STORE_ERR_FULL) {
        return ESP_ERR_NO_MEM;
    }
    if (result != UI_TEXT_STORE_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!changed) {
        return ESP_OK;
    }
    return ui_text_persist();
}

esp_err_t ui_text_withdraw_package(const char *package_id) {
    if (!ui_text_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    bool changed = false;
    const ui_text_store_result_t result = ui_text_store_withdraw(
        &ui_text_store,
        package_id,
        &changed
    );
    if (result != UI_TEXT_STORE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!changed) {
        return ESP_OK;
    }
    return ui_text_persist();
}

esp_err_t ui_text_resolve(
    const char *ui_text_key,
    const char *fallback,
    const char **value
) {
    if (ui_text_key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t position = 0;
    if (ui_text_store_find(&ui_text_store, ui_text_key, &position) ==
        UI_TEXT_STORE_OK) {
        *value = ui_text_store.entries[position].value;
        return ESP_OK;
    }
    if (fallback != NULL && fallback[0] != '\0') {
        *value = fallback;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

const char *ui_text_error_name(ui_text_error_t error) {
    switch (error) {
        case UI_TEXT_OK:
            return "ok";
        case UI_TEXT_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case UI_TEXT_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case UI_TEXT_ERR_FULL:
            return "full";
        case UI_TEXT_ERR_NOT_FOUND:
            return "not_found";
        case UI_TEXT_ERR_STORAGE:
            return "storage";
        case UI_TEXT_ERR_CORRUPT_INDEX:
            return "corrupt_index";
        default:
            return "unknown";
    }
}

const module_descriptor_t *ui_text_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "ui_text",
        .version = "1.0.0",
        .initialize = ui_text_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
