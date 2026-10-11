#include "playback_history.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "playback_history_core.h"

// The bounded history ring persists as one binary blob so "继续播放" survives a
// restart without a dedicated partition.
#define PLAYBACK_HISTORY_STORE_KEY "playback_history_index"

static playback_history_core_t s_core;
static bool s_ready;

static esp_err_t playback_history_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        PLAYBACK_HISTORY_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void playback_history_restore(void) {
    playback_history_core_t restored = {0};
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        PLAYBACK_HISTORY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= PLAYBACK_HISTORY_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid ring so a
    // corrupt store never blocks startup.
    playback_history_core_init(&s_core);
}

esp_err_t playback_history_init(void) {
    playback_history_core_init(&s_core);
    playback_history_restore();
    s_ready = true;
    return ESP_OK;
}

bool playback_history_is_ready(void) {
    return s_ready;
}

esp_err_t playback_history_record(
    const playback_record_t *record,
    size_t *index_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const playback_history_error_t result = playback_history_core_record(
        &s_core,
        record
    );
    if (result != PLAYBACK_HISTORY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index_out != NULL) {
        *index_out = 0;
    }
    return playback_history_persist();
}

esp_err_t playback_history_list(
    playback_record_t *records_out,
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

size_t playback_history_count(void) {
    return s_core.count;
}

bool playback_history_latest_resumable(playback_record_t *record_out) {
    return playback_history_core_latest_resumable(&s_core, record_out);
}

const char *playback_outcome_name(playback_outcome_t outcome) {
    switch (outcome) {
        case PLAYBACK_OUTCOME_COMPLETED:
            return "completed";
        case PLAYBACK_OUTCOME_INTERRUPTED:
            return "interrupted";
        default:
            return "unknown";
    }
}

const char *playback_history_error_name(playback_history_error_t error) {
    switch (error) {
        case PLAYBACK_HISTORY_OK:
            return "ok";
        case PLAYBACK_HISTORY_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case PLAYBACK_HISTORY_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t playback_history_initialize(void) {
    return playback_history_init();
}

const module_descriptor_t *playback_history_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "playback_history",
        .version = "1.0.0",
        .initialize = playback_history_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
