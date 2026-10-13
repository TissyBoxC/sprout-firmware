#include "video_companion_report_core.h"

#include <stddef.h>

video_companion_status_t video_companion_report_build(
    const video_session_status_t *status
) {
    video_companion_status_t report = {
        .call_state = "idle",
        .quality = "unknown",
        .camera_allowed = false,
        .microphone_allowed = false,
        .last_call_id = "",
    };
    if (status == NULL) {
        return report;
    }
    report.camera_allowed = status->camera_allowed;
    report.microphone_allowed = status->microphone_allowed;
    report.call_state = video_state_name(status->call_state);
    report.quality = video_quality_name(status->quality);
    // A device that may not capture is never reported as being in a live call,
    // so a revoked permission cannot leave a ringing or active claim behind.
    if (!status->camera_allowed && !status->microphone_allowed) {
        report.call_state = "idle";
        report.quality = "unknown";
    }
    return report;
}
