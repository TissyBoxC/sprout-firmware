#include "device_capabilities.h"

#include <stddef.h>
#include <string.h>

static const char *const CAPABILITY_NAMES[DEVICE_CAPABILITY_COUNT] = {
    "audio_input",
    "audio_output",
    "wifi",
    "camera",
    "display",
    "touch",
    "led",
    "battery",
    "cellular_4g",
    "motion",
    "bluetooth_audio",
    "video_call",
    "location",
    "geofence",
    "sos",
    "multi_device",
};

static const device_capability_bitmap_t CONFIGURED_CAPABILITIES =
#ifdef CONFIG_CAPABILITY_AUDIO_INPUT
    (1U << DEVICE_CAPABILITY_AUDIO_INPUT) |
#endif
#ifdef CONFIG_CAPABILITY_AUDIO_OUTPUT
    (1U << DEVICE_CAPABILITY_AUDIO_OUTPUT) |
#endif
#ifdef CONFIG_CAPABILITY_WIFI
    (1U << DEVICE_CAPABILITY_WIFI) |
#endif
#ifdef CONFIG_CAPABILITY_CAMERA
    (1U << DEVICE_CAPABILITY_CAMERA) |
#endif
#ifdef CONFIG_CAPABILITY_DISPLAY
    (1U << DEVICE_CAPABILITY_DISPLAY) |
#endif
#ifdef CONFIG_CAPABILITY_TOUCH
    (1U << DEVICE_CAPABILITY_TOUCH) |
#endif
#ifdef CONFIG_CAPABILITY_LED
    (1U << DEVICE_CAPABILITY_LED) |
#endif
#ifdef CONFIG_CAPABILITY_BATTERY
    (1U << DEVICE_CAPABILITY_BATTERY) |
#endif
#ifdef CONFIG_CAPABILITY_CELLULAR_4G
    (1U << DEVICE_CAPABILITY_CELLULAR_4G) |
#endif
#ifdef CONFIG_CAPABILITY_MOTION
    (1U << DEVICE_CAPABILITY_MOTION) |
#endif
#ifdef CONFIG_CAPABILITY_BLUETOOTH_AUDIO
    (1U << DEVICE_CAPABILITY_BLUETOOTH_AUDIO) |
#endif
#ifdef CONFIG_CAPABILITY_VIDEO_CALL
    (1U << DEVICE_CAPABILITY_VIDEO_CALL) |
#endif
#ifdef CONFIG_CAPABILITY_LOCATION
    (1U << DEVICE_CAPABILITY_LOCATION) |
#endif
#ifdef CONFIG_CAPABILITY_GEOFENCE
    (1U << DEVICE_CAPABILITY_GEOFENCE) |
#endif
#ifdef CONFIG_CAPABILITY_SOS
    (1U << DEVICE_CAPABILITY_SOS) |
#endif
#ifdef CONFIG_CAPABILITY_MULTI_DEVICE
    (1U << DEVICE_CAPABILITY_MULTI_DEVICE) |
#endif
    0U;

_Static_assert(
    sizeof(CAPABILITY_NAMES) / sizeof(CAPABILITY_NAMES[0]) ==
        DEVICE_CAPABILITY_COUNT,
    "CAPABILITY_NAMES must match the device capability contract"
);

const char *device_capabilities_contract_version(void) {
    return "1.0.0";
}

device_capability_bitmap_t device_capabilities_get(void) {
    return CONFIGURED_CAPABILITIES;
}

bool device_capabilities_has(
    device_capability_bitmap_t capabilities,
    device_capability_t capability
) {
    if (capability < 0 || capability >= DEVICE_CAPABILITY_COUNT) {
        return false;
    }
    return (capabilities & (1U << capability)) != 0U;
}

const char *device_capability_name(device_capability_t capability) {
    if (capability < 0 || capability >= DEVICE_CAPABILITY_COUNT) {
        return NULL;
    }
    return CAPABILITY_NAMES[capability];
}

bool device_capability_from_name(
    const char *capability_name,
    device_capability_t *capability_out
) {
    if (capability_name == NULL || capability_out == NULL) {
        return false;
    }

    for (device_capability_t capability = DEVICE_CAPABILITY_AUDIO_INPUT;
         capability < DEVICE_CAPABILITY_COUNT;
         ++capability) {
        if (strcmp(CAPABILITY_NAMES[capability], capability_name) == 0) {
            *capability_out = capability;
            return true;
        }
    }
    return false;
}

static esp_err_t device_capabilities_initialize(void) {
    // The platform contract requires at least one advertised capability.
    return CONFIGURED_CAPABILITIES != 0U ? ESP_OK : ESP_ERR_INVALID_STATE;
}

const module_descriptor_t *device_capabilities_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_capabilities",
        .version = "1.0.0",
        .initialize = device_capabilities_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
