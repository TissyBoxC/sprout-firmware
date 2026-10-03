#include "volume_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_FEATURE_CONFIG_STORE
#include "config_store.h"
#endif

static const char *const TAG = "volume_control";

#define VOLUME_CONTROL_VOLUME_KEY "audio_volume"
#define VOLUME_CONTROL_MAX_KEY "audio_volume_max"
#define VOLUME_CONTROL_MUTED_KEY "audio_muted"
#define VOLUME_CONTROL_VALUE_SIZE 8

// The effective and stored values are read by the playback task and written by
// the policy path, so every access goes through one mutex. The gain helper runs
// on the audio task and only reads the cached percentage.
static SemaphoreHandle_t volume_control_mutex;
static bool volume_control_ready;
static uint8_t volume_control_volume_percent;
static uint8_t volume_control_max_percent;
static bool volume_control_muted;

static uint8_t volume_control_clamp(uint8_t percent) {
    if (percent > volume_control_max_percent) {
        return volume_control_max_percent;
    }
    return percent;
}

#if CONFIG_FEATURE_CONFIG_STORE
static void volume_control_load_persisted(void) {
    char stored_value[VOLUME_CONTROL_VALUE_SIZE] = {0};

    if (config_store_get_string(
            VOLUME_CONTROL_MAX_KEY,
            stored_value,
            sizeof(stored_value)
        ) == ESP_OK) {
        const int parsed = atoi(stored_value);
        if (parsed >= VOLUME_CONTROL_PERCENT_MIN &&
            parsed <= VOLUME_CONTROL_PERCENT_MAX) {
            volume_control_max_percent = (uint8_t)parsed;
        }
    }

    if (config_store_get_string(
            VOLUME_CONTROL_VOLUME_KEY,
            stored_value,
            sizeof(stored_value)
        ) == ESP_OK) {
        const int parsed = atoi(stored_value);
        if (parsed >= VOLUME_CONTROL_PERCENT_MIN &&
            parsed <= VOLUME_CONTROL_PERCENT_MAX) {
            volume_control_volume_percent = (uint8_t)parsed;
        }
    }

    if (config_store_get_string(
            VOLUME_CONTROL_MUTED_KEY,
            stored_value,
            sizeof(stored_value)
        ) == ESP_OK) {
        volume_control_muted = strcmp(stored_value, "1") == 0;
    }

    // A persisted value written under a higher policy cap must not survive a
    // stricter guardian limit, so the clamp happens after both reads.
    volume_control_volume_percent =
        volume_control_clamp(volume_control_volume_percent);
}

static esp_err_t volume_control_persist(
    const char *key,
    uint8_t value
) {
    char encoded[VOLUME_CONTROL_VALUE_SIZE] = {0};
    const int written = snprintf(encoded, sizeof(encoded), "%u", (unsigned)value);
    if (written <= 0 || (size_t)written >= sizeof(encoded)) {
        return ESP_FAIL;
    }
    return config_store_set_string(key, encoded);
}
#endif

esp_err_t volume_control_init(void) {
    if (volume_control_ready) {
        return ESP_OK;
    }
    if (volume_control_mutex == NULL) {
        volume_control_mutex = xSemaphoreCreateMutex();
        if (volume_control_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const int configured_default = CONFIG_VOLUME_CONTROL_DEFAULT_PERCENT;
    volume_control_max_percent =
        (uint8_t)CONFIG_VOLUME_CONTROL_MAX_PERCENT;
    volume_control_volume_percent = volume_control_max_percent;
    if (configured_default >= VOLUME_CONTROL_PERCENT_MIN &&
        configured_default <= VOLUME_CONTROL_PERCENT_MAX) {
        volume_control_volume_percent = (uint8_t)configured_default;
    }
    volume_control_volume_percent =
        volume_control_clamp(volume_control_volume_percent);
    volume_control_muted = false;

#if CONFIG_FEATURE_CONFIG_STORE
    if (!config_store_is_ready()) {
        // Without the store the module still enforces the compiled default, so
        // a profile that removes config_store keeps working with bounded state.
        ESP_LOGW(TAG, "config store unavailable; using compiled volume defaults");
    } else {
        volume_control_load_persisted();
    }
#endif

    volume_control_ready = true;
    ESP_LOGI(
        TAG,
        "volume ready: volume=%u max=%u",
        (unsigned)volume_control_volume_percent,
        (unsigned)volume_control_max_percent
    );
    return ESP_OK;
}

bool volume_control_is_ready(void) {
    return volume_control_ready;
}

volume_control_error_t volume_control_set_percent(uint8_t volume_percent) {
    if (!volume_control_ready) {
        return VOLUME_CONTROL_ERR_NOT_INITIALIZED;
    }
    if (volume_percent > VOLUME_CONTROL_PERCENT_MAX) {
        return VOLUME_CONTROL_ERR_INVALID_PERCENT;
    }
    if (xSemaphoreTake(volume_control_mutex, portMAX_DELAY) != pdTRUE) {
        return VOLUME_CONTROL_ERR_STORAGE;
    }

    volume_control_volume_percent = volume_control_clamp(volume_percent);
    const uint8_t persisted = volume_control_volume_percent;
    xSemaphoreGive(volume_control_mutex);

#if CONFIG_FEATURE_CONFIG_STORE
    if (config_store_is_ready()) {
        const esp_err_t result = volume_control_persist(
            VOLUME_CONTROL_VOLUME_KEY,
            persisted
        );
        if (result != ESP_OK) {
            // Failing to persist is reported but does not undo the live volume,
            // because the child still needs an immediate, bounded change.
            ESP_LOGW(TAG, "volume persistence failed: %s", esp_err_to_name(result));
            return VOLUME_CONTROL_ERR_STORAGE;
        }
    }
#endif
    return VOLUME_CONTROL_OK;
}

volume_control_error_t volume_control_set_max_percent(uint8_t max_volume_percent) {
    if (!volume_control_ready) {
        return VOLUME_CONTROL_ERR_NOT_INITIALIZED;
    }
    if (max_volume_percent > VOLUME_CONTROL_PERCENT_MAX) {
        return VOLUME_CONTROL_ERR_INVALID_PERCENT;
    }
    if (xSemaphoreTake(volume_control_mutex, portMAX_DELAY) != pdTRUE) {
        return VOLUME_CONTROL_ERR_STORAGE;
    }

    volume_control_max_percent = max_volume_percent;
    volume_control_volume_percent =
        volume_control_clamp(volume_control_volume_percent);
    const uint8_t persisted_volume = volume_control_volume_percent;
    xSemaphoreGive(volume_control_mutex);

#if CONFIG_FEATURE_CONFIG_STORE
    if (config_store_is_ready()) {
        esp_err_t result = volume_control_persist(
            VOLUME_CONTROL_MAX_KEY,
            max_volume_percent
        );
        if (result == ESP_OK) {
            result = volume_control_persist(
                VOLUME_CONTROL_VOLUME_KEY,
                persisted_volume
            );
        }
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "max volume persistence failed: %s",
                     esp_err_to_name(result));
            return VOLUME_CONTROL_ERR_STORAGE;
        }
    }
#endif
    return VOLUME_CONTROL_OK;
}

uint8_t volume_control_get_percent(void) {
    if (volume_control_muted) {
        return VOLUME_CONTROL_PERCENT_MIN;
    }
    return volume_control_volume_percent;
}

uint8_t volume_control_get_max_percent(void) {
    return volume_control_max_percent;
}

esp_err_t volume_control_set_muted(bool is_muted) {
    if (!volume_control_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(volume_control_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    volume_control_muted = is_muted;
    xSemaphoreGive(volume_control_mutex);

#if CONFIG_FEATURE_CONFIG_STORE
    if (config_store_is_ready()) {
        const esp_err_t result = config_store_set_string(
            VOLUME_CONTROL_MUTED_KEY,
            is_muted ? "1" : "0"
        );
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "mute persistence failed: %s", esp_err_to_name(result));
            return result;
        }
    }
#endif
    return ESP_OK;
}

bool volume_control_is_muted(void) {
    return volume_control_muted;
}

volume_control_snapshot_t volume_control_get_snapshot(void) {
    volume_control_snapshot_t snapshot = {
        .volume_percent = volume_control_get_percent(),
        .max_volume_percent = volume_control_max_percent,
        .is_muted = volume_control_muted,
    };
    return snapshot;
}

const char *volume_control_error_name(volume_control_error_t error) {
    switch (error) {
        case VOLUME_CONTROL_OK:
            return "ok";
        case VOLUME_CONTROL_ERR_NOT_INITIALIZED:
            // "Not initialized yet" is the same retryable condition as a codec
            // that has not opened, so the shared catalog exposes one code.
            return "audio_codec_not_initialized";
        case VOLUME_CONTROL_ERR_INVALID_PERCENT:
            return "audio_volume_invalid";
        case VOLUME_CONTROL_ERR_STORAGE:
        default:
            return "audio_codec_internal";
    }
}

esp_err_t volume_control_apply_gain(int16_t *pcm_samples, size_t sample_count) {
    if (pcm_samples == NULL && sample_count != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count == 0) {
        return ESP_OK;
    }

    if (volume_control_muted) {
        memset(pcm_samples, 0, sample_count * sizeof(int16_t));
        return ESP_OK;
    }

    const uint8_t percent = volume_control_volume_percent;
    if (percent >= VOLUME_CONTROL_PERCENT_MAX) {
        // Full scale needs no arithmetic, which also avoids rounding a
        // loud passage down by one least-significant bit.
        return ESP_OK;
    }

    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t scaled =
            ((int32_t)pcm_samples[index] * (int32_t)percent) / 100;
        if (scaled > INT16_MAX) {
            pcm_samples[index] = INT16_MAX;
        } else if (scaled < INT16_MIN) {
            pcm_samples[index] = INT16_MIN;
        } else {
            pcm_samples[index] = (int16_t)scaled;
        }
    }
    return ESP_OK;
}

const module_descriptor_t *volume_control_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "volume_control",
        .version = "1.0.0",
        .initialize = volume_control_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
