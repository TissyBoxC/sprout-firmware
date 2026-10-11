#include "form_factor_profile.h"

#include <stddef.h>

#include "config_store.h"

// One bounded binary blob persists the composed form-factor profile without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define FORM_FACTOR_STORE_KEY "form_factor"

static form_factor_profile_t s_profile;
static bool s_ready;

static esp_err_t form_factor_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        FORM_FACTOR_STORE_KEY,
        &s_profile,
        sizeof(s_profile)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void form_factor_restore(void) {
    form_factor_profile_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        FORM_FACTOR_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        form_factor_is_valid(&restored)) {
        s_profile = restored;
        return;
    }
    s_profile = form_factor_default();
}

esp_err_t form_factor_init(void) {
    form_factor_restore();
    s_ready = true;
    return ESP_OK;
}

bool form_factor_is_ready(void) {
    return s_ready;
}

esp_err_t form_factor_apply(const form_factor_profile_t *profile) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (profile == NULL || !form_factor_is_valid(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    // The bounded profile is the execution contract; the runtime only reads
    // validated fields from here.
    s_profile = *profile;
    return ESP_OK;
}

esp_err_t form_factor_set(const form_factor_profile_t *profile) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_profile = form_factor_clamp(profile);
    return form_factor_persist();
}

form_factor_profile_t form_factor_get(void) {
    return s_profile;
}

static esp_err_t form_factor_initialize(void) {
    return form_factor_init();
}

const module_descriptor_t *form_factor_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "form_factor_profile",
        .version = "1.0.0",
        .initialize = form_factor_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
