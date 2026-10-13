#include "growth_record.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "growth_record_core.h"

// The bounded growth record store persists as one binary blob so the device
// keeps the guardian's recent timeline across restarts without a dedicated
// partition.
#define GROWTH_RECORD_STORE_KEY "growth_index"
CONFIG_STORE_KEY_ASSERT(GROWTH_RECORD_STORE_KEY);

static growth_record_core_t s_core;
static bool s_ready;

static esp_err_t growth_record_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        GROWTH_RECORD_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void growth_record_restore(void) {
    growth_record_core_t restored = {0};
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        GROWTH_RECORD_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= GROWTH_RECORD_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid store so a
    // corrupt store never blocks startup.
    growth_record_core_init(&s_core);
}

esp_err_t growth_record_init(void) {
    growth_record_core_init(&s_core);
    growth_record_restore();
    s_ready = true;
    return ESP_OK;
}

bool growth_record_is_ready(void) {
    return s_ready;
}

esp_err_t growth_record_upsert(const growth_record_t *record) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const growth_record_error_t result = growth_record_core_upsert(
        &s_core,
        record
    );
    if (result != GROWTH_RECORD_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return growth_record_persist();
}

esp_err_t growth_record_remove(const char *record_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const growth_record_error_t result = growth_record_core_remove(
        &s_core,
        record_id
    );
    if (result != GROWTH_RECORD_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return growth_record_persist();
}

esp_err_t growth_record_remove_child(
    const char *child_id,
    size_t *removed_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const growth_record_error_t result = growth_record_core_remove_child(
        &s_core,
        child_id,
        removed_out
    );
    if (result != GROWTH_RECORD_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return growth_record_persist();
}

esp_err_t growth_record_list(
    growth_record_t *records_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (records_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        records_out[index] = s_core.records[index];
    }
    *count_out = s_core.count;
    return ESP_OK;
}

bool growth_record_get(const char *record_id, growth_record_t *record_out) {
    if (record_id == NULL || record_out == NULL) {
        return false;
    }
    const size_t index = growth_record_core_find(&s_core, record_id);
    if (index >= s_core.count) {
        return false;
    }
    *record_out = s_core.records[index];
    return true;
}

size_t growth_record_count(void) {
    return s_core.count;
}

const char *growth_record_category_name(growth_category_t category) {
    switch (category) {
        case GROWTH_CATEGORY_MILESTONE:
            return "milestone";
        case GROWTH_CATEGORY_ENCOURAGEMENT:
            return "encouragement";
        case GROWTH_CATEGORY_HABIT:
            return "habit";
        case GROWTH_CATEGORY_EMOTION:
            return "emotion";
        default:
            return "unknown";
    }
}

bool growth_record_category_from_name(
    const char *name,
    growth_category_t *category_out
) {
    if (name == NULL) {
        return false;
    }
    growth_category_t parsed;
    if (strcmp(name, "milestone") == 0) {
        parsed = GROWTH_CATEGORY_MILESTONE;
    } else if (strcmp(name, "encouragement") == 0) {
        parsed = GROWTH_CATEGORY_ENCOURAGEMENT;
    } else if (strcmp(name, "habit") == 0) {
        parsed = GROWTH_CATEGORY_HABIT;
    } else if (strcmp(name, "emotion") == 0) {
        parsed = GROWTH_CATEGORY_EMOTION;
    } else {
        return false;
    }
    if (category_out != NULL) {
        *category_out = parsed;
    }
    return true;
}

const char *growth_record_error_name(growth_record_error_t error) {
    switch (error) {
        case GROWTH_RECORD_OK:
            return "ok";
        case GROWTH_RECORD_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case GROWTH_RECORD_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case GROWTH_RECORD_ERR_FULL:
            return "full";
        case GROWTH_RECORD_ERR_NOT_FOUND:
            return "not_found";
        case GROWTH_RECORD_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t growth_record_initialize(void) {
    return growth_record_init();
}

const module_descriptor_t *growth_record_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "growth_record",
        .version = "1.0.0",
        .initialize = growth_record_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
