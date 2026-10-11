#include "video_session.h"

#include <cassert>

int main() {
    const video_policy_t defaults = video_default();
    assert(video_is_valid(&defaults));
    assert(defaults.camera_allowed == 0u);
    assert(defaults.microphone_allowed == 0u);
    assert(defaults.require_child_consent == 1u);
    assert(defaults.ring_timeout_seconds == 30u);
    // The default policy cannot capture until a guardian enables a permission.
    assert(!video_can_capture(&defaults));

    video_policy_t out_of_range = defaults;
    out_of_range.ring_timeout_seconds = 1u;
    out_of_range.max_session_seconds = 65000u;
    out_of_range.quality_floor = 200u;
    const video_policy_t clamped = video_clamp(&out_of_range);
    assert(clamped.ring_timeout_seconds == VIDEO_MIN_RING_TIMEOUT_SECONDS);
    assert(clamped.max_session_seconds == VIDEO_MAX_MAX_SESSION_SECONDS);
    assert(clamped.quality_floor == (uint8_t)VIDEO_QUALITY_POOR);
    assert(video_is_valid(&clamped));

    assert(!video_is_valid(nullptr));
    video_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!video_is_valid(&wrong_version));
    assert(video_clamp(nullptr).ring_timeout_seconds ==
           defaults.ring_timeout_seconds);

    // Capture needs at least one media permission and an enabled policy.
    video_policy_t mic_only = defaults;
    mic_only.microphone_allowed = 1u;
    assert(video_can_capture(&mic_only));
    video_policy_t disabled = mic_only;
    disabled.enabled = 0u;
    assert(!video_can_capture(&disabled));

    // The ring window trips inclusively.
    assert(!video_ring_timed_out(&defaults, 29000u));
    assert(video_ring_timed_out(&defaults, 30000u));
    assert(video_ring_timed_out(&defaults, 30001u));
    return 0;
}
