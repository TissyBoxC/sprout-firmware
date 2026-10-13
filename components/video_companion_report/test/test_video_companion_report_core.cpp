#include "video_companion_report_core.h"

#include <cassert>
#include <cstring>

static bool state_is(const char *actual, const char *expected) {
    return std::strcmp(actual, expected) == 0;
}

int main() {
    // A null snapshot reports the safe idle default rather than guessing.
    const video_companion_status_t empty = video_companion_report_build(nullptr);
    assert(state_is(empty.call_state, "idle"));
    assert(state_is(empty.quality, "unknown"));
    assert(empty.camera_allowed == false);
    assert(empty.microphone_allowed == false);
    assert(state_is(empty.last_call_id, ""));

    // An active call with both permissions reports the live state and quality.
    const video_session_status_t active = {
        .supported = true,
        .camera_allowed = true,
        .microphone_allowed = true,
        .call_state = VIDEO_STATE_ACTIVE,
        .quality = VIDEO_QUALITY_GOOD,
        .last_end_reason = VIDEO_END_NONE,
        .revision = 4,
    };
    const video_companion_status_t live =
        video_companion_report_build(&active);
    assert(state_is(live.call_state, "active"));
    assert(state_is(live.quality, "good"));
    assert(live.camera_allowed == true);
    assert(live.microphone_allowed == true);

    // Every firmware call state maps to its stable platform name.
    video_session_status_t ringing = active;
    ringing.call_state = VIDEO_STATE_RINGING;
    assert(state_is(
        video_companion_report_build(&ringing).call_state,
        "ringing"
    ));
    video_session_status_t ended = active;
    ended.call_state = VIDEO_STATE_ENDED;
    assert(state_is(
        video_companion_report_build(&ended).call_state,
        "ended"
    ));
    video_session_status_t rejected = active;
    rejected.call_state = VIDEO_STATE_REJECTED;
    assert(state_is(
        video_companion_report_build(&rejected).call_state,
        "rejected"
    ));
    video_session_status_t failed = active;
    failed.call_state = VIDEO_STATE_FAILED;
    assert(state_is(
        video_companion_report_build(&failed).call_state,
        "failed"
    ));
    video_session_status_t idle = active;
    idle.call_state = VIDEO_STATE_IDLE;
    assert(state_is(
        video_companion_report_build(&idle).call_state,
        "idle"
    ));

    // Every firmware quality band maps to its stable platform name.
    video_session_status_t poor = active;
    poor.quality = VIDEO_QUALITY_POOR;
    assert(state_is(
        video_companion_report_build(&poor).quality,
        "poor"
    ));
    video_session_status_t fair = active;
    fair.quality = VIDEO_QUALITY_FAIR;
    assert(state_is(
        video_companion_report_build(&fair).quality,
        "fair"
    ));
    video_session_status_t unknown_quality = active;
    unknown_quality.quality = VIDEO_QUALITY_UNKNOWN;
    assert(state_is(
        video_companion_report_build(&unknown_quality).quality,
        "unknown"
    ));

    // A device with no capture permission never reports a live call, so a
    // revoked permission cannot leave a ringing or active claim behind.
    video_session_status_t no_media = active;
    no_media.camera_allowed = false;
    no_media.microphone_allowed = false;
    const video_companion_status_t muted =
        video_companion_report_build(&no_media);
    assert(state_is(muted.call_state, "idle"));
    assert(state_is(muted.quality, "unknown"));
    assert(muted.camera_allowed == false);
    assert(muted.microphone_allowed == false);

    // A camera-only call still reports its live state.
    video_session_status_t camera_only = active;
    camera_only.microphone_allowed = false;
    const video_companion_status_t camera_report =
        video_companion_report_build(&camera_only);
    assert(state_is(camera_report.call_state, "active"));
    assert(camera_report.camera_allowed == true);
    assert(camera_report.microphone_allowed == false);

    return 0;
}
