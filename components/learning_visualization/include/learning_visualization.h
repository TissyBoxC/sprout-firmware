#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable learning-visualization schema version shared with the platform. */
#define LEARNING_VISUALIZATION_SCHEMA_VERSION 1u

/** @brief Size of a linked lesson id including the terminator. */
#define LEARNING_LESSON_ID_SIZE 65

/** @brief Approved bounds, mirroring the platform contract exactly. */
#define LEARNING_MIN_SESSION_MINUTES 1u
#define LEARNING_MAX_SESSION_MINUTES 60u
#define LEARNING_MAX_FEEDBACK_EVENTS 100000u

/** @brief The fixed feedback-style catalog shared with the platform. */
typedef enum {
    LEARNING_STYLE_NEUTRAL = 0,
    LEARNING_STYLE_ENCOURAGING,
    LEARNING_STYLE_PLAYFUL,
    LEARNING_STYLE_CELEBRATION,
} learning_style_t;

/** @brief The fixed animation-level catalog shared with the platform. */
typedef enum {
    LEARNING_ANIMATION_OFF = 0,
    LEARNING_ANIMATION_SUBTLE,
    LEARNING_ANIMATION_FULL,
} learning_animation_level_t;

/** @brief The normalized visual states shared with the platform. */
typedef enum {
    LEARNING_VISUAL_IDLE = 0,
    LEARNING_VISUAL_LESSON,
    LEARNING_VISUAL_FEEDBACK,
    LEARNING_VISUAL_REWARD,
    LEARNING_VISUAL_PAUSED,
    LEARNING_VISUAL_ERROR,
} learning_visual_state_t;

/**
 * @brief One bounded learning-visualization policy for a family.
 *
 * The struct mirrors the platform policy field for field. The feature and the
 * animation default to off so a device never animates a learning surface
 * without an explicit guardian decision.
 */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t feedback_style;
    uint8_t animation_level;
    uint8_t touch_feedback_enabled;
    uint8_t reward_effects_enabled;
    uint8_t lesson_linkage_enabled;
    uint8_t max_session_minutes;
    uint8_t reserved;
} learning_policy_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    LEARNING_VISUALIZATION_OK = 0,
    LEARNING_VISUALIZATION_ERR_NULL_ARGUMENT,
    LEARNING_VISUALIZATION_ERR_INVALID_POLICY,
    LEARNING_VISUALIZATION_ERR_INVALID_STATE,
    LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED,
    LEARNING_VISUALIZATION_ERR_STORAGE,
} learning_error_t;

/**
 * @brief One guardian policy as carried by the device endpoint.
 *
 * The style and animation level are catalog names ("neutral", "encouraging",
 * "playful", "celebration" and "off", "subtle", "full") rather than the
 * firmware enum so the network layer can populate them without knowing the
 * firmware catalog. The strings are owned by the caller.
 */
typedef struct {
    bool enabled;
    const char *feedback_style;
    const char *animation_level;
    bool touch_feedback_enabled;
    bool reward_effects_enabled;
    bool lesson_linkage_enabled;
    int max_session_minutes;
} learning_visualization_remote_t;

/** @brief Return the conservative default policy (feature and animation off). */
learning_policy_t learning_default(void);

/** @brief Clamp one policy into the documented bounds. */
learning_policy_t learning_clamp(const learning_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool learning_is_valid(const learning_policy_t *policy);

/** @brief Return true when the policy permits the visualization to run. */
bool learning_can_visualize(const learning_policy_t *policy);

/** @brief Return true when a feedback style value is in the catalog. */
bool learning_is_valid_style(learning_style_t style);

/** @brief Return true when an animation level value is in the catalog. */
bool learning_is_valid_animation_level(learning_animation_level_t level);

/** @brief Return true when a visual state value is in the catalog. */
bool learning_is_valid_visual_state(learning_visual_state_t state);

/**
 * @brief Resolve the animation a state should actually run under a policy.
 *
 * A disabled policy or a paused/idle surface resolves to animation off, so a
 * requested full animation never reaches the driver while the surface is not
 * running a lesson or feedback.
 */
learning_animation_level_t learning_effective_animation(
    const learning_policy_t *policy,
    learning_visual_state_t state
);

/** @brief Return true when the session deadline has been reached. */
bool learning_session_expired(
    const learning_policy_t *policy,
    uint32_t elapsed_minutes
);

/** @brief Return true when a linked lesson id is well-formed and bounded. */
bool learning_lesson_id_valid(const char *lesson_id);

/** @brief Return the stable string for one learning error code. */
const char *learning_error_name(learning_error_t error);

/** @brief Return the stable string for one feedback style. */
const char *learning_style_name(learning_style_t style);

/** @brief Return the stable string for one animation level. */
const char *learning_animation_level_name(learning_animation_level_t level);

/** @brief Return the stable string for one visual state. */
const char *learning_visual_state_name(learning_visual_state_t state);

/** @brief Initialize the learning-visualization store and surface state. */
esp_err_t learning_visualization_init(void);

/** @brief Return true when the learning-visualization store is ready. */
bool learning_visualization_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t learning_visualization_set(const learning_policy_t *policy);

/**
 * @brief Apply a policy fetched from the device endpoint.
 *
 * Maps the catalog style and animation names to the firmware enums and clamps
 * every field. Returns ESP_ERR_INVALID_ARG when a name is not in its catalog,
 * so a malformed response is rejected instead of silently defaulting.
 */
esp_err_t learning_visualization_apply_remote(
    const learning_visualization_remote_t *remote
);

/** @brief Return the currently applied policy. */
learning_policy_t learning_visualization_get(void);

/** @brief Apply a policy that is already version-compatible and bounded. */
esp_err_t learning_visualization_apply(const learning_policy_t *policy);

/**
 * @brief Begin a lesson on the touch surface and link the lesson id.
 *
 * The lesson only starts when the policy is enabled and lesson linkage is
 * authorised, so the device never links a lesson a guardian disabled.
 */
esp_err_t learning_visualization_begin_lesson(const char *lesson_id);

/** @brief Record one feedback event and enter the feedback state. */
esp_err_t learning_visualization_report_feedback(void);

/** @brief Enter the reward state and count one reward event. */
esp_err_t learning_visualization_show_reward(void);

/** @brief Pause the running lesson, pinning the animation off. */
esp_err_t learning_visualization_pause(void);

/** @brief Resume a paused lesson when the policy still allows it. */
esp_err_t learning_visualization_resume(void);

/** @brief Return to idle and clear the linked lesson. */
esp_err_t learning_visualization_stop(void);

/** @brief Report a bounded surface error. */
esp_err_t learning_visualization_report_error(void);

/** @brief Return the current visual state. */
learning_visual_state_t learning_visualization_visual_state(void);

/** @brief Return the animation the surface is currently running. */
learning_animation_level_t learning_visualization_animation_level(void);

/** @brief Return the currently linked lesson id, or an empty string. */
const char *learning_visualization_linked_lesson(void);

/** @brief Return how many feedback events the surface has counted. */
uint32_t learning_visualization_feedback_events(void);

/** @brief Return the removable-module descriptor for learning_visualization. */
const module_descriptor_t *learning_visualization_module_descriptor(void);

#ifdef __cplusplus
}
#endif
