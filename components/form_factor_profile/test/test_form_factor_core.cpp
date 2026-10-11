#include "form_factor_profile.h"

#include <cassert>
#include <cstring>

int main() {
    const form_factor_profile_t desktop = form_factor_default();
    assert(form_factor_is_valid(&desktop));
    assert(std::strcmp(desktop.technical_name, "desktop") == 0);
    assert(form_factor_has_capability(&desktop, FORM_FACTOR_CAP_DISPLAY));
    assert(form_factor_has_action(&desktop, FORM_FACTOR_ACTION_EXPRESSION));
    assert(form_factor_has_action(&desktop, FORM_FACTOR_ACTION_VOICE));
    // A desktop body has no locomotion or tail.
    assert(!form_factor_has_action(&desktop, FORM_FACTOR_ACTION_WALK));
    assert(!form_factor_has_action(&desktop, FORM_FACTOR_ACTION_TAIL));

    // Composition: a moving body with a servo gains walk, dance, tail and
    // gesture.
    const uint32_t moving_caps =
        FORM_FACTOR_CAP_LOCOMOTION | FORM_FACTOR_CAP_SERVO;
    const uint32_t moving_actions = form_factor_compose_actions(moving_caps);
    assert((moving_actions & FORM_FACTOR_ACTION_WALK) != 0u);
    assert((moving_actions & FORM_FACTOR_ACTION_DANCE) != 0u);
    assert((moving_actions & FORM_FACTOR_ACTION_TAIL) != 0u);
    assert((moving_actions & FORM_FACTOR_ACTION_GESTURE) != 0u);
    assert((moving_actions & FORM_FACTOR_ACTION_VOICE) == 0u);

    // Voice needs both a microphone and a speaker; a camera and display add a
    // video call, and a wearable adds location.
    assert((form_factor_compose_actions(FORM_FACTOR_CAP_MICROPHONE) &
            FORM_FACTOR_ACTION_VOICE) == 0u);
    assert((form_factor_compose_actions(
                FORM_FACTOR_CAP_MICROPHONE | FORM_FACTOR_CAP_SPEAKER) &
            FORM_FACTOR_ACTION_VOICE) != 0u);
    assert((form_factor_compose_actions(
                FORM_FACTOR_CAP_CAMERA | FORM_FACTOR_CAP_DISPLAY) &
            FORM_FACTOR_ACTION_VIDEO_CALL) != 0u);
    assert((form_factor_compose_actions(FORM_FACTOR_CAP_WEARABLE) &
            FORM_FACTOR_ACTION_LOCATION) != 0u);

    // Clamp masks unknown capability bits and recomposes the action set.
    form_factor_profile_t unknown = desktop;
    unknown.capability_mask = 0xFFFFFFFFu;
    const form_factor_profile_t clamped = form_factor_clamp(&unknown);
    assert(clamped.capability_mask == FORM_FACTOR_CAP_ALL);
    assert(clamped.action_mask ==
           form_factor_compose_actions(FORM_FACTOR_CAP_ALL));
    assert(form_factor_is_valid(&clamped));
    assert(form_factor_clamp(nullptr).capability_mask ==
           desktop.capability_mask);

    // Inconsistent actions are rejected.
    form_factor_profile_t inconsistent = desktop;
    inconsistent.action_mask |= FORM_FACTOR_ACTION_WALK;
    assert(!form_factor_is_valid(&inconsistent));
    assert(!form_factor_is_valid(nullptr));
    form_factor_profile_t wrong_version = desktop;
    wrong_version.schema_version = 99u;
    assert(!form_factor_is_valid(&wrong_version));

    // An empty name is not a valid profile.
    form_factor_profile_t unnamed = desktop;
    std::memset(unnamed.technical_name, 0, sizeof(unnamed.technical_name));
    assert(!form_factor_is_valid(&unnamed));

    assert(std::strcmp(
               form_factor_capability_name(FORM_FACTOR_CAP_LOCOMOTION),
               "locomotion") == 0);
    assert(std::strcmp(
               form_factor_action_name(FORM_FACTOR_ACTION_TAIL),
               "tail") == 0);
    return 0;
}
