#include "form_factor_report_core.h"

#include <cassert>
#include <cstring>

static device_capability_bitmap_t cap(device_capability_t capability) {
    return (device_capability_bitmap_t)(1U << capability);
}

int main() {
    // A desktop body maps to display, audio, touch and battery.
    const device_capability_bitmap_t desktop =
        cap(DEVICE_CAPABILITY_DISPLAY) |
        cap(DEVICE_CAPABILITY_AUDIO_INPUT) |
        cap(DEVICE_CAPABILITY_AUDIO_OUTPUT) |
        cap(DEVICE_CAPABILITY_TOUCH) |
        cap(DEVICE_CAPABILITY_BATTERY);
    const uint32_t desktop_mask = form_factor_report_capabilities(desktop);
    assert((desktop_mask & FORM_FACTOR_CAP_DISPLAY) != 0u);
    assert((desktop_mask & FORM_FACTOR_CAP_MICROPHONE) != 0u);
    assert((desktop_mask & FORM_FACTOR_CAP_SPEAKER) != 0u);
    assert((desktop_mask & FORM_FACTOR_CAP_TOUCH) != 0u);
    assert((desktop_mask & FORM_FACTOR_CAP_BATTERY) != 0u);
    assert((desktop_mask & FORM_FACTOR_CAP_CAMERA) == 0u);
    assert((desktop_mask & FORM_FACTOR_CAP_LOCOMOTION) == 0u);

    // An empty capability set maps to no form-factor capability.
    assert(form_factor_report_capabilities(0u) == 0u);

    // A camera and motion body maps to the locomotion and camera bits.
    const device_capability_bitmap_t moving =
        cap(DEVICE_CAPABILITY_CAMERA) |
        cap(DEVICE_CAPABILITY_MOTION) |
        cap(DEVICE_CAPABILITY_CELLULAR_4G);
    const uint32_t moving_mask = form_factor_report_capabilities(moving);
    assert((moving_mask & FORM_FACTOR_CAP_CAMERA) != 0u);
    assert((moving_mask & FORM_FACTOR_CAP_LOCOMOTION) != 0u);
    assert((moving_mask & FORM_FACTOR_CAP_CELLULAR) != 0u);

    // A wearable capability maps to the wearable bit, which composes location.
    const uint32_t wearable_mask =
        form_factor_report_capabilities(cap(DEVICE_CAPABILITY_LOCATION));
    assert((wearable_mask & FORM_FACTOR_CAP_WEARABLE) != 0u);

    // Building a report from a NULL profile yields a valid default.
    form_factor_profile_t report = {};
    const device_capability_bitmap_t all_caps =
        (device_capability_bitmap_t)((1U << DEVICE_CAPABILITY_COUNT) - 1U);
    form_factor_report_build(nullptr, all_caps, &report);
    assert(report.schema_version == FORM_FACTOR_SCHEMA_VERSION);
    assert(std::strcmp(report.technical_name, "desktop") == 0);
    assert(report.capability_mask == form_factor_default().capability_mask);
    assert(report.action_mask == form_factor_compose_actions(
        report.capability_mask));

    // A profile that names hardware the image does not compile is reduced to
    // the compiled capabilities only.
    form_factor_profile_t desired = form_factor_default();
    form_factor_profile_t constrained = {};
    form_factor_report_build(&desired, cap(DEVICE_CAPABILITY_BATTERY),
                             &constrained);
    assert(constrained.capability_mask == FORM_FACTOR_CAP_BATTERY);
    assert((constrained.capability_mask & FORM_FACTOR_CAP_DISPLAY) == 0u);
    assert(constrained.action_mask == 0u);

    // An over-long local name is truncated to fit the platform bound.
    form_factor_profile_t long_name = {};
    long_name.schema_version = FORM_FACTOR_SCHEMA_VERSION;
    long_name.capability_mask = FORM_FACTOR_CAP_DISPLAY;
    const char *long_value = "an-extremely-long-technical-name";
    std::memcpy(
        long_name.technical_name,
        long_value,
        sizeof(long_name.technical_name) - 1
    );
    long_name.technical_name[sizeof(long_name.technical_name) - 1] = '\0';
    form_factor_profile_t built = {};
    form_factor_report_build(&long_name, all_caps, &built);
    assert(built.technical_name[FORM_FACTOR_NAME_MAX - 1u] == '\0');
    assert(std::strlen(built.technical_name) <= FORM_FACTOR_NAME_MAX - 1u);

    // The action mask always follows the capability mask, so a smuggled action
    // bit is dropped even when the caller set one directly.
    form_factor_profile_t tampered = {};
    tampered.schema_version = FORM_FACTOR_SCHEMA_VERSION;
    tampered.capability_mask = FORM_FACTOR_CAP_SPEAKER;
    tampered.action_mask = FORM_FACTOR_ACTION_WALK;
    form_factor_report_build(&tampered, all_caps, &built);
    assert(built.action_mask == form_factor_compose_actions(
        FORM_FACTOR_CAP_SPEAKER));
    assert((built.action_mask & FORM_FACTOR_ACTION_WALK) == 0u);

    // An unknown capability bit above the contract is dropped.
    tampered.capability_mask = 0xFFFFFFFFu;
    form_factor_report_build(&tampered, all_caps, &built);
    assert((built.capability_mask & ~FORM_FACTOR_CAP_ALL) == 0u);

    return 0;
}
