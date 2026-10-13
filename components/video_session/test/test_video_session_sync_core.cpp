#include "video_session_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // Every catalog quality floor maps to its enum; an unknown is rejected.
    video_quality_t quality = VIDEO_QUALITY_UNKNOWN;
    assert(video_session_sync_core_parse_quality("unknown", &quality));
    assert(quality == VIDEO_QUALITY_UNKNOWN);
    assert(video_session_sync_core_parse_quality("good", &quality));
    assert(quality == VIDEO_QUALITY_GOOD);
    assert(video_session_sync_core_parse_quality("fair", &quality));
    assert(quality == VIDEO_QUALITY_FAIR);
    assert(video_session_sync_core_parse_quality("poor", &quality));
    assert(quality == VIDEO_QUALITY_POOR);
    assert(!video_session_sync_core_parse_quality("ultra", &quality));
    assert(!video_session_sync_core_parse_quality(nullptr, &quality));

    // A fully populated enabled policy is clamped and mapped.
    video_remote_t remote = {};
    remote.enabled = true;
    remote.camera_allowed = true;
    remote.microphone_allowed = true;
    remote.require_child_consent = false;
    remote.quality_floor = "fair";
    remote.ring_timeout_seconds = 5;
    remote.max_session_seconds = 999999;
    remote.revision = 9;
    video_policy_t policy = {};
    assert(video_session_sync_core_build_policy(&remote, &policy));
    assert(policy.schema_version == VIDEO_SESSION_SCHEMA_VERSION);
    assert(policy.enabled == 1u);
    assert(policy.camera_allowed == 1u);
    assert(policy.microphone_allowed == 1u);
    assert(policy.require_child_consent == 0u);
    assert(policy.quality_floor == (uint8_t)VIDEO_QUALITY_FAIR);
    assert(policy.ring_timeout_seconds == 5u);
    assert(policy.max_session_seconds == VIDEO_MAX_MAX_SESSION_SECONDS);
    assert(policy.revision == 9u);
    assert(video_is_valid(&policy));
    assert(video_can_capture(&policy));

    // The ring timeout and session length clamp into their approved bounds.
    video_remote_t tiny = remote;
    tiny.ring_timeout_seconds = 0;
    tiny.max_session_seconds = 0;
    video_policy_t tiny_policy = {};
    assert(video_session_sync_core_build_policy(&tiny, &tiny_policy));
    assert(tiny_policy.ring_timeout_seconds == VIDEO_MIN_RING_TIMEOUT_SECONDS);
    assert(tiny_policy.max_session_seconds == VIDEO_MIN_MAX_SESSION_SECONDS);
    video_remote_t huge = remote;
    huge.ring_timeout_seconds = 999999;
    video_policy_t huge_policy = {};
    assert(video_session_sync_core_build_policy(&huge, &huge_policy));
    assert(huge_policy.ring_timeout_seconds == VIDEO_MAX_RING_TIMEOUT_SECONDS);

    // A negative timeout floors at the minimum.
    video_remote_t negative = remote;
    negative.ring_timeout_seconds = -5;
    negative.max_session_seconds = -5;
    video_policy_t negative_policy = {};
    assert(video_session_sync_core_build_policy(&negative, &negative_policy));
    assert(negative_policy.ring_timeout_seconds ==
           VIDEO_MIN_RING_TIMEOUT_SECONDS);
    assert(negative_policy.max_session_seconds ==
           VIDEO_MIN_MAX_SESSION_SECONDS);

    // A disabled policy or a policy with no media permission cannot capture.
    video_remote_t disabled = remote;
    disabled.enabled = false;
    video_policy_t disabled_policy = {};
    assert(video_session_sync_core_build_policy(&disabled, &disabled_policy));
    assert(disabled_policy.enabled == 0u);
    assert(!video_can_capture(&disabled_policy));
    video_remote_t no_media = remote;
    no_media.camera_allowed = false;
    no_media.microphone_allowed = false;
    video_policy_t no_media_policy = {};
    assert(video_session_sync_core_build_policy(&no_media, &no_media_policy));
    assert(!video_can_capture(&no_media_policy));

    // An unknown quality floor rejects the whole policy.
    video_remote_t bad = remote;
    bad.quality_floor = "ultra";
    video_policy_t bad_policy = {};
    assert(!video_session_sync_core_build_policy(&bad, &bad_policy));

    return 0;
}
