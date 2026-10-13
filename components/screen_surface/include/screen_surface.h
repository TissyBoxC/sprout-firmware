#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stable screen-surface schema version shared with the platform contract.
#define SCREEN_SURFACE_SCHEMA_VERSION 1u

// Bounds for the video playback profile the surface will accept.
#define SCREEN_MIN_VIDEO_BITRATE_KBPS 100u
#define SCREEN_MAX_VIDEO_BITRATE_KBPS 8000u
#define SCREEN_MIN_VIDEO_FPS 5u
#define SCREEN_MAX_VIDEO_FPS 60u

// Bounds for the animation frame interval in milliseconds.
#define SCREEN_MIN_ANIMATION_FRAME_MS 40u
#define SCREEN_MAX_ANIMATION_FRAME_MS 1000u

// Bounds for the point-to-read dictionary.
#define SCREEN_POINT_READ_MAX_TERM 48u
#define SCREEN_POINT_READ_MAX_TEXT 96u
#define SCREEN_POINT_READ_CAPACITY 32u

// One bounded facial expression the surface can render.
typedef enum {
    SCREEN_EXPRESSION_NEUTRAL = 0,
    SCREEN_EXPRESSION_HAPPY,
    SCREEN_EXPRESSION_SAD,
    SCREEN_EXPRESSION_CURIOUS,
    SCREEN_EXPRESSION_SLEEPY,
    SCREEN_EXPRESSION_SURPRISED,
    SCREEN_EXPRESSION_EXCITED,
} screen_expression_t;

// One built-in animation clip.
typedef enum {
    SCREEN_ANIMATION_NONE = 0,
    SCREEN_ANIMATION_IDLE,
    SCREEN_ANIMATION_BLINK,
    SCREEN_ANIMATION_WAVE,
    SCREEN_ANIMATION_NOD,
    SCREEN_ANIMATION_DANCE,
} screen_animation_t;

// One video playback state shared with the platform.
typedef enum {
    SCREEN_VIDEO_IDLE = 0,
    SCREEN_VIDEO_LOADING,
    SCREEN_VIDEO_PLAYING,
    SCREEN_VIDEO_PAUSED,
    SCREEN_VIDEO_ENDED,
    SCREEN_VIDEO_FAILED,
} screen_video_state_t;

// One bounded screen-surface policy.
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t default_expression;
    uint8_t animation_enabled;
    uint8_t point_read_enabled;
    uint8_t video_enabled;
    uint8_t video_max_fps;
    uint16_t video_max_bitrate_kbps;
    uint16_t animation_frame_ms;
    uint32_t revision;
} screen_policy_t;

// One point-to-read dictionary entry. `text` is what the surface speaks or
// shows when the child points at the matching term.
typedef struct {
    char term[SCREEN_POINT_READ_MAX_TERM + 1];
    char text[SCREEN_POINT_READ_MAX_TEXT + 1];
} screen_point_read_entry_t;

// One clamped video playback profile the surface will actually use.
typedef struct {
    uint16_t bitrate_kbps;
    uint8_t fps;
} screen_video_plan_t;

// Stable error codes for diagnostics and guardian-facing copy.
typedef enum {
    SCREEN_OK = 0,
    SCREEN_ERR_NULL_ARGUMENT,
    SCREEN_ERR_INVALID_POLICY,
    SCREEN_ERR_INVALID_EXPRESSION,
    SCREEN_ERR_INVALID_ANIMATION,
    SCREEN_ERR_INVALID_STATE,
    SCREEN_ERR_STORAGE,
    SCREEN_ERR_NOT_FOUND,
    SCREEN_ERR_FULL,
} screen_error_t;

// Return the conservative default policy: a neutral face with animation and
// point-to-read on, video enabled at a low bitrate so a slow link still plays.
screen_policy_t screen_default(void);

// Clamp one policy into the documented bounds.
screen_policy_t screen_clamp(const screen_policy_t *policy);

// Return true when a policy is version-compatible and bounded.
bool screen_is_valid(const screen_policy_t *policy);

// Return the stable string for one screen error code.
const char *screen_error_name(screen_error_t error);

// Return the stable string for one expression.
const char *screen_expression_name(screen_expression_t expression);

// Return the stable string for one animation clip.
const char *screen_animation_name(screen_animation_t animation);

// Return the stable string for one video state.
const char *screen_video_state_name(screen_video_state_t state);

// Return true when the value maps to a known expression.
bool screen_expression_valid(uint32_t expression);

// Return true when the value maps to a known animation clip.
bool screen_animation_valid(uint32_t animation);

// Clamp a requested video profile into both the platform bounds and the
// policy bounds, so an unsupported stream never reaches the decoder.
screen_video_plan_t screen_video_plan(
    const screen_policy_t *policy,
    uint32_t requested_bitrate_kbps,
    uint32_t requested_fps
);

// Return true when the animation frame interval has elapsed. The comparison is
// inclusive so a frame exactly at the bound advances in host tests and on the
// device alike.
bool screen_animation_frame_due(
    const screen_policy_t *policy,
    uint32_t elapsed_ms
);

// Return true when the entry term matches the looked-up term exactly after
// trimming leading and trailing spaces. An empty term never matches.
bool screen_point_read_matches(
    const screen_point_read_entry_t *entry,
    const char *term
);

// Initialize the screen-surface store.
esp_err_t screen_surface_init(void);

// Return true when the screen-surface store is ready.
bool screen_surface_is_ready(void);

// Store a guardian policy, clamping out-of-range values.
esp_err_t screen_surface_set(const screen_policy_t *policy);

// Return the currently applied policy.
screen_policy_t screen_surface_get(void);

// Apply a policy that is already version-compatible and bounded.
esp_err_t screen_surface_apply(const screen_policy_t *policy);

// Show one expression, clamped to the policy default when the surface is off.
esp_err_t screen_surface_show_expression(screen_expression_t expression);

// Return the expression the surface is currently showing.
screen_expression_t screen_surface_current_expression(void);

// Start one animation clip. Passing SCREEN_ANIMATION_NONE stops the clip.
esp_err_t screen_surface_play_animation(screen_animation_t animation);

// Return the animation clip currently playing, or SCREEN_ANIMATION_NONE.
screen_animation_t screen_surface_current_animation(void);

// Return the animation frame index for the current clip, advancing the frame
// clock by `elapsed_ms`. A clip that is not playing stays at frame zero.
uint32_t screen_surface_advance_animation(uint32_t elapsed_ms);

// Add or replace one point-to-read entry. A duplicate term replaces its text.
esp_err_t screen_surface_point_read_put(const screen_point_read_entry_t *entry);

// Look up one term. Returns SCREEN_ERR_NOT_FOUND when no entry matches.
esp_err_t screen_surface_point_read_lookup(
    const char *term,
    screen_point_read_entry_t *output
);

// Return the number of point-to-read entries currently loaded.
size_t screen_surface_point_read_count(void);

// Remove every point-to-read entry.
void screen_surface_point_read_clear(void);

// Begin video playback for one requested profile, returning the clamped plan.
esp_err_t screen_surface_video_begin(
    uint32_t requested_bitrate_kbps,
    uint32_t requested_fps,
    screen_video_plan_t *output
);

// Pause the current video. Returns SCREEN_ERR_INVALID_STATE when not playing.
esp_err_t screen_surface_video_pause(void);

// Resume the current video. Returns SCREEN_ERR_INVALID_STATE when not paused.
esp_err_t screen_surface_video_resume(void);

// Stop the current video and return to idle.
esp_err_t screen_surface_video_stop(void);

// Return the current video playback state.
screen_video_state_t screen_surface_video_state(void);

// Return the removable-module descriptor for screen_surface.
const module_descriptor_t *screen_surface_module_descriptor(void);

#ifdef __cplusplus
}
#endif
