#include "display_policy.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "display_policy_core.h"

// One bounded binary blob keeps the guardian policy across restarts without a
// dedicated partition; the core store is versioned so a schema change is
// detected and replaced with the conservative default instead of applied.
#define DISPLAY_POLICY_STORE_KEY "display_policy"

static display_policy_t s_policy;
static display_policy_usage_t s_usage;
static bool s_ready;

static esp_err_t display_policy_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        DISPLAY_POLICY_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void display_policy_restore(void) {
    display_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        DISPLAY_POLICY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        display_policy_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = display_policy_default();
}

esp_err_t display_policy_init(void) {
    display_policy_restore();
    display_policy_usage_init(&s_usage);
    s_ready = true;
    return ESP_OK;
}

bool display_policy_is_ready(void) {
    return s_ready;
}

esp_err_t display_policy_apply(const display_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !display_policy_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    // The bounded policy is the execution contract; the hardware driver only
    // consumes validated fields from here.
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t display_policy_set(const display_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const display_policy_t clamped = display_policy_clamp(policy);
    s_policy = clamped;
    return display_policy_persist();
}

display_policy_t display_policy_get(void) {
    return s_policy;
}

uint32_t display_policy_report_activity(uint32_t now_epoch_seconds) {
    if (!s_ready) {
        return 0u;
    }
    if (!s_usage.break_window_active) {
        display_policy_usage_start_break_window(&s_usage, now_epoch_seconds);
    }
    display_policy_usage_add_second(&s_usage, now_epoch_seconds);
    return s_usage.used_seconds;
}

void display_policy_reset_usage(uint32_t now_epoch_seconds) {
    if (!s_ready) {
        return;
    }
    display_policy_usage_start_break_window(&s_usage, now_epoch_seconds);
}

bool display_policy_break_due(uint32_t now_epoch_seconds) {
    if (!s_ready) {
        return false;
    }
    return display_policy_usage_break_due(
        &s_usage,
        s_policy.break_interval_minutes,
        now_epoch_seconds
    );
}

bool display_policy_daily_limit_reached(
    uint32_t used_seconds_today,
    uint32_t now_epoch_seconds
) {
    if (!s_ready) {
        return false;
    }
    const uint32_t limit_seconds =
        (uint32_t)s_policy.daily_limit_minutes * 60u;
    return used_seconds_today >= limit_seconds &&
        display_policy_day_key(now_epoch_seconds) != 0u;
}

const char *display_policy_error_name(display_policy_error_t error) {
    switch (error) {
        case DISPLAY_POLICY_OK:
            return "ok";
        case DISPLAY_POLICY_ERR_NULL_ARGUMENT:
            return "not_initialized";
        case DISPLAY_POLICY_ERR_INVALID_POLICY:
            return "invalid_policy";
        case DISPLAY_POLICY_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t display_policy_initialize(void) {
    return display_policy_init();
}

const module_descriptor_t *display_policy_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "display_policy",
        .version = "1.0.0",
        .initialize = display_policy_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
