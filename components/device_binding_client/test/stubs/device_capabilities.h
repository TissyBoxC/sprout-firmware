#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVICE_CAPABILITY_AUDIO_INPUT = 0,
    DEVICE_CAPABILITY_AUDIO_OUTPUT,
    DEVICE_CAPABILITY_WIFI,
    DEVICE_CAPABILITY_CAMERA,
    DEVICE_CAPABILITY_DISPLAY,
    DEVICE_CAPABILITY_TOUCH,
    DEVICE_CAPABILITY_LED,
    DEVICE_CAPABILITY_BATTERY,
    DEVICE_CAPABILITY_CELLULAR_4G,
    DEVICE_CAPABILITY_MOTION,
    DEVICE_CAPABILITY_BLUETOOTH_AUDIO,
    DEVICE_CAPABILITY_VIDEO_CALL,
    DEVICE_CAPABILITY_LOCATION,
    DEVICE_CAPABILITY_GEOFENCE,
    DEVICE_CAPABILITY_SOS,
    DEVICE_CAPABILITY_MULTI_DEVICE,
    DEVICE_CAPABILITY_COUNT,
} device_capability_t;

typedef uint32_t device_capability_bitmap_t;

device_capability_bitmap_t device_capabilities_get(void);
bool device_capabilities_has(
    device_capability_bitmap_t capabilities,
    device_capability_t capability
);
const char *device_capability_name(device_capability_t capability);

#ifdef __cplusplus
}
#endif
