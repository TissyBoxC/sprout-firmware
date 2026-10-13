#include "video_session.h"

#include <cassert>

int main() {
    assert(video_init() == ESP_OK);
    assert(video_is_ready());
    assert(video_state() == VIDEO_STATE_IDLE);

    const video_policy_t defaults = video_get();
    assert(defaults.camera_allowed == 0u);

    // The default policy refuses a ring because no media is permitted.
    assert(video_ring() == ESP_ERR_INVALID_STATE);

    // Enable the microphone and ring; the call stays ringing until accepted.
    video_policy_t policy = defaults;
    policy.microphone_allowed = 1u;
    assert(video_set(&policy) == ESP_OK);
    assert(video_ring() == ESP_OK);
    assert(video_state() == VIDEO_STATE_RINGING);
    assert(video_accept() == ESP_OK);
    assert(video_state() == VIDEO_STATE_ACTIVE);

    // A quality report is bounded.
    assert(video_report_quality(VIDEO_QUALITY_GOOD) == ESP_OK);
    assert(video_quality() == VIDEO_QUALITY_GOOD);
    assert(video_report_quality((video_quality_t)99) == ESP_ERR_INVALID_ARG);

    // Revoking the microphone during an active call ends it immediately.
    assert(video_revoke_permission(false, true) == ESP_OK);
    assert(video_state() == VIDEO_STATE_FAILED);
    assert(video_last_end_reason() == VIDEO_END_PERMISSION_REVOKED);
    assert(video_get().microphone_allowed == 0u);

    // A rejected call is a distinct terminal state.
    video_policy_t reenabled = video_get();
    reenabled.camera_allowed = 1u;
    assert(video_set(&reenabled) == ESP_OK);
    assert(video_ring() == ESP_OK);
    assert(video_end(VIDEO_END_REJECTED) == ESP_OK);
    assert(video_state() == VIDEO_STATE_REJECTED);

    // A network drop maps to the failed state.
    assert(video_ring() == ESP_OK);
    assert(video_accept() == ESP_OK);
    assert(video_end(VIDEO_END_NETWORK) == ESP_OK);
    assert(video_state() == VIDEO_STATE_FAILED);

    // Ending twice in a row is rejected.
    assert(video_end(VIDEO_END_HANGUP) == ESP_ERR_INVALID_STATE);

    // The status snapshot mirrors the live call state and policy revision.
    video_policy_t revisioned = video_get();
    revisioned.enabled = 1u;
    revisioned.camera_allowed = 1u;
    revisioned.microphone_allowed = 1u;
    revisioned.revision = 5u;
    assert(video_set(&revisioned) == ESP_OK);
    assert(video_ring() == ESP_OK);
    assert(video_accept() == ESP_OK);
    assert(video_report_quality(VIDEO_QUALITY_FAIR) == ESP_OK);
    video_session_status_t status = video_session_status();
    assert(status.supported);
    assert(status.camera_allowed);
    assert(status.microphone_allowed);
    assert(status.call_state == VIDEO_STATE_ACTIVE);
    assert(status.quality == VIDEO_QUALITY_FAIR);
    assert(status.revision == 5u);
    assert(video_end(VIDEO_END_HANGUP) == ESP_OK);
    assert(video_session_status().last_end_reason == VIDEO_END_HANGUP);

    // A disabled policy reports an idle call with no capture permission.
    video_policy_t disabled = video_get();
    disabled.enabled = 0u;
    disabled.camera_allowed = 1u;
    disabled.microphone_allowed = 1u;
    assert(video_set(&disabled) == ESP_OK);
    video_session_status_t idle = video_session_status();
    assert(!idle.camera_allowed);
    assert(!idle.microphone_allowed);
    assert(idle.call_state == VIDEO_STATE_IDLE);
    assert(idle.quality == VIDEO_QUALITY_UNKNOWN);
    assert(idle.last_end_reason == VIDEO_END_NONE);

    // Persistence round trip keeps the edited policy.
    video_policy_t persisted = video_get();
    persisted.camera_allowed = 0u;
    persisted.microphone_allowed = 0u;
    assert(video_set(&persisted) == ESP_OK);
    assert(video_init() == ESP_OK);
    assert(video_get().camera_allowed == 0u);
    assert(video_get().microphone_allowed == 0u);

    // A policy that is not version-compatible is refused by apply.
    video_policy_t invalid;
    invalid.schema_version = 42u;
    assert(video_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(video_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = video_session_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
