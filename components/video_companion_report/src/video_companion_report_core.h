#pragma once

#include <stdbool.h>

#include "video_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One bounded video-companionship status ready to report.
 *
 * This mirrors the fields the platform's video-companion device status
 * endpoint accepts. The platform stores it as the device's latest call and
 * media-permission state so the guardian capability view can read it back.
 */
typedef struct {
    const char *call_state;
    const char *quality;
    bool camera_allowed;
    bool microphone_allowed;
    const char *last_call_id;
} video_companion_status_t;

/**
 * @brief Build one reportable video-companion status from the session
 * snapshot.
 *
 * The call state and quality band are mapped to their stable platform names.
 * A device with no capture permission reports the idle state with no last call
 * so a revoked permission never leaves a live-call claim behind. The snapshot
 * already collapses a disabled policy to idle, so this only widens the guard
 * to a device that lost its media permissions while idle.
 */
video_companion_status_t video_companion_report_build(
    const video_session_status_t *status
);

#ifdef __cplusplus
}
#endif
