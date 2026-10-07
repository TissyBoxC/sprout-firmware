#include "child_prompt_profile.h"

#include <string.h>

#include "config_store.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_FEATURE_PARENT_POLICY
#include "parent_policy.h"
#endif

static const char *const TAG = "child_prompt";

#define CHILD_PROMPT_PROFILE_TIER_KEY "child_age_tier"

// The conservative default is the youngest tier: local prompts stay simplest
// and most restricted until the platform profile supplies a real value.
#define CHILD_PROMPT_PROFILE_DEFAULT_TIER \
    CHILD_PROMPT_PROFILE_TIER_AGE_3_4

typedef struct {
    bool is_initialized;
    child_prompt_profile_tier_t tier;
    bool tier_persisted;
    child_prompt_profile_snapshot_t categories;
} child_prompt_profile_state_t;

static child_prompt_profile_state_t child_prompt_profile_state;

static bool child_prompt_profile_tier_is_valid(
    child_prompt_profile_tier_t tier
) {
    return tier >= CHILD_PROMPT_PROFILE_TIER_AGE_3_4 &&
           tier <= CHILD_PROMPT_PROFILE_TIER_AGE_7_8;
}

const char *child_prompt_profile_tier_name(child_prompt_profile_tier_t tier) {
    switch (tier) {
        case CHILD_PROMPT_PROFILE_TIER_AGE_3_4:
            return "age_3_4";
        case CHILD_PROMPT_PROFILE_TIER_AGE_5_6:
            return "age_5_6";
        case CHILD_PROMPT_PROFILE_TIER_AGE_7_8:
            return "age_7_8";
        case CHILD_PROMPT_PROFILE_TIER_UNKNOWN:
        default:
            return NULL;
    }
}

bool child_prompt_profile_tier_from_name(
    const char *tier_text,
    child_prompt_profile_tier_t *tier_out
) {
    if (tier_text == NULL || tier_out == NULL) {
        return false;
    }
    if (strcmp(tier_text, "age_3_4") == 0) {
        *tier_out = CHILD_PROMPT_PROFILE_TIER_AGE_3_4;
        return true;
    }
    if (strcmp(tier_text, "age_5_6") == 0) {
        *tier_out = CHILD_PROMPT_PROFILE_TIER_AGE_5_6;
        return true;
    }
    if (strcmp(tier_text, "age_7_8") == 0) {
        *tier_out = CHILD_PROMPT_PROFILE_TIER_AGE_7_8;
        return true;
    }
    return false;
}

static esp_err_t child_prompt_profile_load_tier(void) {
    char tier_text[CHILD_PROMPT_PROFILE_TIER_TEXT_SIZE] = {0};
    const esp_err_t read_result = config_store_get_string(
        CHILD_PROMPT_PROFILE_TIER_KEY,
        tier_text,
        sizeof(tier_text)
    );
    if (read_result == CONFIG_STORE_ERR_NOT_FOUND) {
        child_prompt_profile_state.tier_persisted = false;
        child_prompt_profile_state.tier = CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
        return ESP_OK;
    }
    if (read_result != ESP_OK) {
        return read_result;
    }

    child_prompt_profile_tier_t parsed = CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
    if (!child_prompt_profile_tier_from_name(tier_text, &parsed)) {
        // A corrupted value must not silently widen the content level.
        ESP_LOGW(TAG, "discarding invalid persisted age tier");
        child_prompt_profile_state.tier_persisted = false;
        child_prompt_profile_state.tier = CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
        return ESP_OK;
    }
    child_prompt_profile_state.tier = parsed;
    child_prompt_profile_state.tier_persisted = true;
    return ESP_OK;
}

static void child_prompt_profile_clear_categories(void) {
    memset(
        &child_prompt_profile_state.categories,
        0,
        sizeof(child_prompt_profile_state.categories)
    );
    child_prompt_profile_state.categories.max_volume_percent = 100;
}

esp_err_t child_prompt_profile_init(void) {
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (child_prompt_profile_state.is_initialized) {
        return ESP_OK;
    }
    child_prompt_profile_clear_categories();

    esp_err_t result = child_prompt_profile_load_tier();
    if (result != ESP_OK) {
        return result;
    }

    child_prompt_profile_state.is_initialized = true;
    (void)child_prompt_profile_sync_from_parent_policy();
    return ESP_OK;
}

bool child_prompt_profile_is_ready(void) {
    return child_prompt_profile_state.is_initialized;
}

esp_err_t child_prompt_profile_set_tier(child_prompt_profile_tier_t tier) {
    if (!child_prompt_profile_tier_is_valid(tier)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!child_prompt_profile_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *tier_text = child_prompt_profile_tier_name(tier);
    if (tier_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t write_result = config_store_set_string(
        CHILD_PROMPT_PROFILE_TIER_KEY,
        tier_text
    );
    if (write_result != ESP_OK) {
        return write_result;
    }
    child_prompt_profile_state.tier = tier;
    child_prompt_profile_state.tier_persisted = true;
    return ESP_OK;
}

esp_err_t child_prompt_profile_set_tier_text(const char *tier_text) {
    child_prompt_profile_tier_t tier = CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
    if (!child_prompt_profile_tier_from_name(tier_text, &tier)) {
        return ESP_ERR_INVALID_ARG;
    }
    return child_prompt_profile_set_tier(tier);
}

child_prompt_profile_tier_t child_prompt_profile_get_tier(void) {
    if (!child_prompt_profile_state.is_initialized) {
        return CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
    }
    if (!child_prompt_profile_state.tier_persisted) {
        // An unset tier resolves to the conservative default so a caller
        // always has an actionable age bucket.
        return CHILD_PROMPT_PROFILE_DEFAULT_TIER;
    }
    return child_prompt_profile_state.tier;
}

esp_err_t child_prompt_profile_copy_tier_text(
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *tier_text =
        child_prompt_profile_tier_name(child_prompt_profile_get_tier());
    if (tier_text == NULL) {
        tier_text = "unknown";
    }
    const size_t required = strlen(tier_text) + 1;
    if (output_size < required) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, tier_text, required);
    return ESP_OK;
}

esp_err_t child_prompt_profile_sync_from_parent_policy(void) {
    if (!child_prompt_profile_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_FEATURE_PARENT_POLICY
    parent_policy_snapshot_t policy;
    const esp_err_t snapshot_result = parent_policy_get_snapshot(&policy);
    if (snapshot_result == ESP_ERR_NOT_FOUND) {
        child_prompt_profile_clear_categories();
        return ESP_OK;
    }
    if (snapshot_result != ESP_OK) {
        return snapshot_result;
    }

    child_prompt_profile_state.categories.has_parent_policy = true;
    child_prompt_profile_state.categories.max_volume_percent =
        policy.max_volume_percent;

    size_t copied = policy.allowed_category_count;
    if (copied > CHILD_PROMPT_PROFILE_MAX_CATEGORIES) {
        copied = CHILD_PROMPT_PROFILE_MAX_CATEGORIES;
    }
    size_t index = 0;
    for (; index < copied; ++index) {
        strncpy(
            child_prompt_profile_state.categories.allowed_categories[index],
            policy.allowed_categories[index],
            CHILD_PROMPT_PROFILE_CATEGORY_SIZE - 1
        );
        child_prompt_profile_state
            .categories.allowed_categories[index]
            [CHILD_PROMPT_PROFILE_CATEGORY_SIZE - 1] = '\0';
    }
    child_prompt_profile_state.categories.allowed_category_count = copied;
    return ESP_OK;
#else
    child_prompt_profile_clear_categories();
    return ESP_OK;
#endif
}

esp_err_t child_prompt_profile_get_snapshot(
    child_prompt_profile_snapshot_t *snapshot_out
) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!child_prompt_profile_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    *snapshot_out = child_prompt_profile_state.categories;
    snapshot_out->tier = child_prompt_profile_get_tier();
    const char *tier_text = child_prompt_profile_tier_name(snapshot_out->tier);
    if (tier_text != NULL) {
        strncpy(
            snapshot_out->tier_text,
            tier_text,
            sizeof(snapshot_out->tier_text) - 1
        );
        snapshot_out->tier_text[sizeof(snapshot_out->tier_text) - 1] = '\0';
    }
    return ESP_OK;
}

bool child_prompt_profile_category_allowed(const char *category) {
    if (category == NULL || !child_prompt_profile_state.is_initialized) {
        return false;
    }
    if (!child_prompt_profile_state.categories.has_parent_policy) {
        // Without a guardian allowlist the device must not claim any category
        // is permitted, so local gating stays denied.
        return false;
    }
    for (size_t index = 0;
         index < child_prompt_profile_state.categories.allowed_category_count;
         ++index) {
        if (strcmp(
                child_prompt_profile_state
                    .categories.allowed_categories[index],
                category
            ) == 0) {
            return true;
        }
    }
    return false;
}

void child_prompt_profile_shutdown(void) {
    child_prompt_profile_state.is_initialized = false;
    child_prompt_profile_state.tier = CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
    child_prompt_profile_state.tier_persisted = false;
    child_prompt_profile_clear_categories();
}

const module_descriptor_t *child_prompt_profile_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "child_prompt_profile",
        .version = "1.0.0",
        .initialize = child_prompt_profile_init,
        .shutdown = child_prompt_profile_shutdown,
    };
    return &descriptor;
}
