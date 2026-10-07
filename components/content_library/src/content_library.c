#include "content_library.h"

#include <string.h>

#include "config_store.h"
#include "esp_log.h"
#include "content_library_index.h"

#ifndef CONFIG_CONTENT_LIBRARY_INDEX_CAPACITY
#define CONFIG_CONTENT_LIBRARY_INDEX_CAPACITY 48
#endif

#ifndef CONFIG_CONTENT_LIBRARY_METADATA_KEY
#define CONFIG_CONTENT_LIBRARY_METADATA_KEY "content_index"
#endif

#define CONTENT_LIBRARY_INDEX_MAGIC 0x53434c31u

static const char *const TAG = "content_library";

typedef struct {
    uint32_t magic;
    uint32_t count;
    content_library_entry_t entries[CONFIG_CONTENT_LIBRARY_INDEX_CAPACITY];
} content_library_persisted_t;

static content_library_entry_t content_library_entries[
    CONFIG_CONTENT_LIBRARY_INDEX_CAPACITY
];
static content_library_persisted_t content_library_persisted;
static content_index_t content_library_index;
static bool content_library_ready;

static esp_err_t content_library_persist(void) {
    memset(&content_library_persisted, 0, sizeof(content_library_persisted));
    content_library_persisted.magic = CONTENT_LIBRARY_INDEX_MAGIC;
    content_library_persisted.count = (uint32_t)content_library_index.count;
    for (size_t index = 0; index < content_library_index.count; ++index) {
        content_library_persisted.entries[index] =
            content_library_index.entries[index];
    }
    const esp_err_t result = config_store_set_blob(
        CONFIG_CONTENT_LIBRARY_METADATA_KEY,
        &content_library_persisted,
        sizeof(content_library_persisted)
    );
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "index persistence failed: %s", esp_err_to_name(result));
    }
    return result;
}

static void content_library_load(void) {
    memset(&content_library_persisted, 0, sizeof(content_library_persisted));
    size_t value_size = sizeof(content_library_persisted);
    const esp_err_t result = config_store_get_blob(
        CONFIG_CONTENT_LIBRARY_METADATA_KEY,
        &content_library_persisted,
        &value_size
    );
    if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        return;
    }
    if (result != ESP_OK || value_size != sizeof(content_library_persisted) ||
        content_library_persisted.magic != CONTENT_LIBRARY_INDEX_MAGIC ||
        content_library_persisted.count > CONFIG_CONTENT_LIBRARY_INDEX_CAPACITY) {
        // A corrupt index must not prevent the device from booting. Clearing
        // it forces a clean manifest sync rather than returning invalid data.
        ESP_LOGW(TAG, "discarding corrupt content index");
        (void)config_store_erase_key(CONFIG_CONTENT_LIBRARY_METADATA_KEY);
        return;
    }
    for (size_t index = 0; index < content_library_persisted.count; ++index) {
        if (!content_index_entry_is_valid(
                &content_library_persisted.entries[index]
            )) {
            ESP_LOGW(TAG, "discarding invalid content index entry");
            (void)config_store_erase_key(CONFIG_CONTENT_LIBRARY_METADATA_KEY);
            return;
        }
    }
    for (size_t index = 0; index < content_library_persisted.count; ++index) {
        content_library_entries[index] =
            content_library_persisted.entries[index];
    }
    content_library_index.count = content_library_persisted.count;
}

esp_err_t content_library_init(void) {
    if (content_library_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const content_index_result_t index_result = content_index_init(
        &content_library_index,
        content_library_entries,
        CONFIG_CONTENT_LIBRARY_INDEX_CAPACITY
    );
    if (index_result != CONTENT_INDEX_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    content_library_load();
    content_library_ready = true;
    return ESP_OK;
}

bool content_library_is_ready(void) {
    return content_library_ready;
}

size_t content_library_count(void) {
    return content_library_ready ? content_library_index.count : 0;
}

esp_err_t content_library_apply_manifest(
    const content_library_manifest_item_t *items,
    size_t item_count
) {
    if (!content_library_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (items == NULL && item_count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t applied = 0;
    const content_index_result_t result = content_index_apply_manifest(
        &content_library_index,
        items,
        item_count,
        &applied
    );
    if (result == CONTENT_INDEX_ERR_INVALID_ARGUMENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (result != CONTENT_INDEX_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (applied == 0) {
        return ESP_OK;
    }
    return content_library_persist();
}

esp_err_t content_library_get(
    const char *package_id,
    content_library_entry_t *entry_out
) {
    if (!content_library_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (package_id == NULL || entry_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t position = 0;
    const content_index_result_t result = content_index_find(
        &content_library_index,
        package_id,
        &position
    );
    if (result == CONTENT_INDEX_ERR_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (result != CONTENT_INDEX_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    *entry_out = content_library_index.entries[position];
    return ESP_OK;
}

esp_err_t content_library_list(
    const char *category,
    int age_tier,
    bool include_all,
    content_library_entry_t *entries_out,
    size_t entry_capacity,
    size_t *match_count_out
) {
    if (match_count_out != NULL) {
        *match_count_out = 0;
    }
    if (!content_library_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (entries_out == NULL && entry_capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (age_tier < -1 || age_tier >= CONTENT_LIBRARY_AGE_TIER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t matches = 0;
    for (size_t index = 0; index < content_library_index.count; ++index) {
        const content_library_entry_t *entry = &content_library_index.entries[index];
        if (!include_all && entry->state != CONTENT_LIBRARY_STATE_READY) {
            continue;
        }
        if (category != NULL && category[0] != '\0' &&
            strcmp(entry->category, category) != 0) {
            continue;
        }
        if (age_tier >= 0 && !entry->age_tiers[age_tier]) {
            continue;
        }
        if (entries_out != NULL && matches < entry_capacity) {
            entries_out[matches] = *entry;
        }
        ++matches;
    }
    if (match_count_out != NULL) {
        *match_count_out = matches;
    }
    return ESP_OK;
}

esp_err_t content_library_update_state(
    const char *package_id,
    content_library_state_t state
) {
    if (!content_library_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (package_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t position = 0;
    const content_index_result_t find_result = content_index_find(
        &content_library_index,
        package_id,
        &position
    );
    if (find_result == CONTENT_INDEX_ERR_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (find_result != CONTENT_INDEX_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (content_library_index.entries[position].state == state) {
        return ESP_OK;
    }
    content_library_index.entries[position].state = state;
    return content_library_persist();
}

esp_err_t content_library_remove(const char *package_id) {
    if (!content_library_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (package_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const content_index_result_t result = content_index_remove(
        &content_library_index,
        package_id
    );
    if (result == CONTENT_INDEX_ERR_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (result != CONTENT_INDEX_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return content_library_persist();
}

const char *content_library_state_name(content_library_state_t state) {
    switch (state) {
        case CONTENT_LIBRARY_STATE_MISSING:
            return "missing";
        case CONTENT_LIBRARY_STATE_DOWNLOADING:
            return "downloading";
        case CONTENT_LIBRARY_STATE_READY:
            return "ready";
        case CONTENT_LIBRARY_STATE_FAILED:
            return "failed";
        case CONTENT_LIBRARY_STATE_WITHDRAWN:
            return "withdrawn";
        default:
            return "unknown";
    }
}

const char *content_library_error_name(content_library_error_t error) {
    switch (error) {
        case CONTENT_LIBRARY_OK:
            return "ok";
        case CONTENT_LIBRARY_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case CONTENT_LIBRARY_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case CONTENT_LIBRARY_ERR_FULL:
            return "full";
        case CONTENT_LIBRARY_ERR_NOT_FOUND:
            return "not_found";
        case CONTENT_LIBRARY_ERR_STORAGE:
            return "storage";
        case CONTENT_LIBRARY_ERR_CORRUPT_INDEX:
            return "corrupt_index";
        default:
            return "unknown";
    }
}

const module_descriptor_t *content_library_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "content_library",
        .version = "1.0.0",
        .initialize = content_library_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
