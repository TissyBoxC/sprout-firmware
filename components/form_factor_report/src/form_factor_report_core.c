#include "form_factor_report_core.h"

#include <string.h>

uint32_t form_factor_report_capabilities(
    device_capability_bitmap_t capability
) {
    uint32_t mask = 0u;
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_DISPLAY)) {
        mask |= FORM_FACTOR_CAP_DISPLAY;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_CAMERA)) {
        mask |= FORM_FACTOR_CAP_CAMERA;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_AUDIO_INPUT)) {
        mask |= FORM_FACTOR_CAP_MICROPHONE;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_AUDIO_OUTPUT)) {
        mask |= FORM_FACTOR_CAP_SPEAKER;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_TOUCH)) {
        mask |= FORM_FACTOR_CAP_TOUCH;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_MOTION)) {
        mask |= FORM_FACTOR_CAP_LOCOMOTION;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_CELLULAR_4G)) {
        mask |= FORM_FACTOR_CAP_CELLULAR;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_BATTERY)) {
        mask |= FORM_FACTOR_CAP_BATTERY;
    }
    if (device_capabilities_has(capability, DEVICE_CAPABILITY_LOCATION)) {
        mask |= FORM_FACTOR_CAP_WEARABLE;
    }
    return mask;
}

void form_factor_report_build(
    const form_factor_profile_t *profile,
    device_capability_bitmap_t compiled,
    form_factor_profile_t *out
) {
    if (out == NULL) {
        return;
    }
    const form_factor_profile_t *source = profile;
    form_factor_profile_t fallback = form_factor_default();
    if (source == NULL) {
        source = &fallback;
    }
    *out = *source;
    out->schema_version = FORM_FACTOR_SCHEMA_VERSION;
    if (out->technical_name[0] == '\0') {
        memset(out->technical_name, 0, sizeof(out->technical_name));
        const char *name = fallback.technical_name;
        memcpy(out->technical_name, name, strlen(name));
    } else {
        out->technical_name[FORM_FACTOR_NAME_MAX - 1u] = '\0';
    }
    // Only claim hardware this image actually compiles, so the guardian view
    // can never show an action the firmware cannot perform.
    out->capability_mask &=
        form_factor_report_capabilities(compiled) & FORM_FACTOR_CAP_ALL;
    out->action_mask = form_factor_compose_actions(out->capability_mask);
}
