#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Longest age-tier token including the terminator. */
#define CHILD_PROMPT_PROFILE_TIER_TEXT_SIZE 16

/** @brief Longest content category token including the terminator. */
#define CHILD_PROMPT_PROFILE_CATEGORY_SIZE 32

/** @brief Maximum categories cached from the guardian policy. */
#define CHILD_PROMPT_PROFILE_MAX_CATEGORIES 16

/**
 * @brief Local age tier controlling child-facing content and prompt wording.
 *
 * The values match packages/contracts/schemas/common.schema.json#/$defs/age_tier
 * so the device tier can be compared with platform and content contracts.
 * When the tier is unknown the device uses the most conservative bucket so a
 * missing policy can never widen the content a child may see.
 */
typedef enum {
    CHILD_PROMPT_PROFILE_TIER_UNKNOWN = 0,
    CHILD_PROMPT_PROFILE_TIER_AGE_3_4,
    CHILD_PROMPT_PROFILE_TIER_AGE_5_6,
    CHILD_PROMPT_PROFILE_TIER_AGE_7_8,
} child_prompt_profile_tier_t;

/** @brief Bounded profile snapshot used by voice_session and local prompts. */
typedef struct {
    child_prompt_profile_tier_t tier;
    char tier_text[CHILD_PROMPT_PROFILE_TIER_TEXT_SIZE];
    uint8_t max_volume_percent;
    char allowed_categories[CHILD_PROMPT_PROFILE_MAX_CATEGORIES]
                           [CHILD_PROMPT_PROFILE_CATEGORY_SIZE];
    size_t allowed_category_count;
    bool has_parent_policy;
} child_prompt_profile_snapshot_t;

/**
 * @brief Load the persisted age tier and the cached guardian policy.
 *
 * Idempotent. Reads the tier from the configuration store; when no tier has
 * ever been persisted the module reports CHILD_PROMPT_PROFILE_TIER_UNKNOWN
 * and callers must apply the conservative default.
 */
esp_err_t child_prompt_profile_init(void);

/** @brief Return true when the profile has been initialized. */
bool child_prompt_profile_is_ready(void);

/**
 * @brief Persist and apply one age tier.
 *
 * Returns ESP_ERR_INVALID_ARG for an unrecognized tier. The value is written
 * before it becomes visible so a failed write cannot silently change the
 * active content level for one boot.
 */
esp_err_t child_prompt_profile_set_tier(child_prompt_profile_tier_t tier);

/** @brief Persist and apply one age tier from its contract text. */
esp_err_t child_prompt_profile_set_tier_text(const char *tier_text);

/** @brief Return the current age tier. */
child_prompt_profile_tier_t child_prompt_profile_get_tier(void);

/** @brief Copy the current tier contract text, or "unknown" when unset. */
esp_err_t child_prompt_profile_copy_tier_text(
    char *output,
    size_t output_size
);

/**
 * @brief Map an age tier to its canonical contract text.
 *
 * Returns NULL for CHILD_PROMPT_PROFILE_TIER_UNKNOWN so callers can handle the
 * unset case explicitly instead of sending a placeholder to the platform.
 */
const char *child_prompt_profile_tier_name(child_prompt_profile_tier_t tier);

/** @brief Resolve contract text to an age tier; false when unknown. */
bool child_prompt_profile_tier_from_name(
    const char *tier_text,
    child_prompt_profile_tier_t *tier_out
);

/**
 * @brief Refresh the cached age tier and category allowlist from parent_policy.
 *
 * The current parent policy contract does not carry the child's age tier, so
 * the age tier keeps its persisted value. This call only mirrors the category
 * allowlist and volume ceiling so local prompts can respect both. It never
 * lowers the age tier, because the platform profile is authoritative for age.
 */
esp_err_t child_prompt_profile_sync_from_parent_policy(void);

/** @brief Copy the bounded current profile snapshot. */
esp_err_t child_prompt_profile_get_snapshot(
    child_prompt_profile_snapshot_t *snapshot_out
);

/** @brief Return true when a category is allowed by the cached policy. */
bool child_prompt_profile_category_allowed(const char *category);

/** @brief Release runtime resources; the persisted tier remains on flash. */
void child_prompt_profile_shutdown(void);

/** @brief Return the removable-module descriptor for child_prompt_profile. */
const module_descriptor_t *child_prompt_profile_module_descriptor(void);

#ifdef __cplusplus
}
#endif
