#pragma once

#include <stdbool.h>

#include "display_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bounded display status ready to report to the platform. */
typedef struct {
    int brightness_percent;
    int screen_width;
    int screen_height;
    bool touch_supported;
} display_status_t;

/**
 * @brief Build a bounded display status from the applied eye-care policy.
 *
 * The brightness is clamped into the platform bound (0..100). The panel size
 * comes from the build configuration, so a device that does not declare one
 * reports zero rather than guessing. Touch support is reported as passed in,
 * derived by the caller from the compiled capability set.
 */
display_status_t display_report_build(
    const display_policy_t *policy,
    int screen_width,
    int screen_height,
    bool touch_supported
);

#ifdef __cplusplus
}
#endif
