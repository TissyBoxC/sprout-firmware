#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable form-factor schema version shared with the platform. */
#define FORM_FACTOR_SCHEMA_VERSION 1u

/** @brief Maximum length of a technical form-factor name including the NUL. */
#define FORM_FACTOR_NAME_MAX 24u

// Capability bits a form factor can advertise. Form factors are named
// technically and never bind to a character or an appearance; the same
// capability set drives both the device and the platform.
#define FORM_FACTOR_CAP_DISPLAY (1u << 0)
#define FORM_FACTOR_CAP_CAMERA (1u << 1)
#define FORM_FACTOR_CAP_MICROPHONE (1u << 2)
#define FORM_FACTOR_CAP_SPEAKER (1u << 3)
#define FORM_FACTOR_CAP_TOUCH (1u << 4)
#define FORM_FACTOR_CAP_LOCOMOTION (1u << 5)
#define FORM_FACTOR_CAP_SERVO (1u << 6)
#define FORM_FACTOR_CAP_CELLULAR (1u << 7)
#define FORM_FACTOR_CAP_BATTERY (1u << 8)
#define FORM_FACTOR_CAP_WEARABLE (1u << 9)

/** @brief Every capability bit the platform recognises. */
#define FORM_FACTOR_CAP_ALL 0x3FFu

// Action bits a composed profile can present. The action set is derived from
// the capability set so swapping a body only swaps the profile and its assets,
// never the voice, content or platform business modules.
#define FORM_FACTOR_ACTION_EXPRESSION (1u << 0)
#define FORM_FACTOR_ACTION_VIDEO_DISPLAY (1u << 1)
#define FORM_FACTOR_ACTION_TOUCH (1u << 2)
#define FORM_FACTOR_ACTION_WALK (1u << 3)
#define FORM_FACTOR_ACTION_DANCE (1u << 4)
#define FORM_FACTOR_ACTION_TAIL (1u << 5)
#define FORM_FACTOR_ACTION_GESTURE (1u << 6)
#define FORM_FACTOR_ACTION_VOICE (1u << 7)
#define FORM_FACTOR_ACTION_VIDEO_CALL (1u << 8)
#define FORM_FACTOR_ACTION_LOCATION (1u << 9)

/** @brief Every action bit the platform recognises. */
#define FORM_FACTOR_ACTION_ALL 0x3FFu

/** @brief One bounded form-factor profile. */
typedef struct {
    uint32_t schema_version;
    char technical_name[FORM_FACTOR_NAME_MAX];
    uint32_t capability_mask;
    uint32_t action_mask;
} form_factor_profile_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    FORM_FACTOR_OK = 0,
    FORM_FACTOR_ERR_NULL_ARGUMENT,
    FORM_FACTOR_ERR_INVALID_PROFILE,
    FORM_FACTOR_ERR_STORAGE,
} form_factor_error_t;

/** @brief Return the conservative default (desktop) profile. */
form_factor_profile_t form_factor_default(void);

/** @brief Clamp one profile into the documented bounds. */
form_factor_profile_t form_factor_clamp(const form_factor_profile_t *profile);

/** @brief Return true when a profile is version-compatible and bounded. */
bool form_factor_is_valid(const form_factor_profile_t *profile);

/** @brief Return the stable string for one form-factor error code. */
const char *form_factor_error_name(form_factor_error_t error);

/** @brief Return the stable string for one capability bit. */
const char *form_factor_capability_name(uint32_t capability);

/** @brief Return the stable string for one action bit. */
const char *form_factor_action_name(uint32_t action);

/** @brief Return true when the profile advertises a capability. */
bool form_factor_has_capability(
    const form_factor_profile_t *profile,
    uint32_t capability
);

/** @brief Return true when the profile exposes an action. */
bool form_factor_has_action(
    const form_factor_profile_t *profile,
    uint32_t action
);

/**
 * @brief Compose the action set implied by a capability set.
 *
 * The composition is the single source of truth: a locomotion body gains walk,
 * dance and gesture; a servo gains a tail and gesture; a display gains
 * expressions and video; a speaker and microphone together gain voice; a camera
 * gains video calls; and a wearable gains location.
 */
uint32_t form_factor_compose_actions(uint32_t capability_mask);

/** @brief Initialize the form-factor profile store. */
esp_err_t form_factor_init(void);

/** @brief Return true when the form-factor profile store is ready. */
bool form_factor_is_ready(void);

/** @brief Store a profile, clamping the mask and recomposing the actions. */
esp_err_t form_factor_set(const form_factor_profile_t *profile);

/** @brief Return the currently applied profile. */
form_factor_profile_t form_factor_get(void);

/** @brief Apply a profile that is already version-compatible and bounded. */
esp_err_t form_factor_apply(const form_factor_profile_t *profile);

/** @brief Return the removable-module descriptor for form_factor_profile. */
const module_descriptor_t *form_factor_module_descriptor(void);

#ifdef __cplusplus
}
#endif
