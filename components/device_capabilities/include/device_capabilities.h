#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The capability names and ordering are part of the 1.0.0 contract. */
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

/** @brief Return the version of the capability contract implemented here. */
const char *device_capabilities_contract_version(void);

/** @brief Return the capabilities compiled into this firmware image. */
device_capability_bitmap_t device_capabilities_get(void);

/** @brief Return whether one capability bit is present in a bitmap. */
bool device_capabilities_has(
    device_capability_bitmap_t capabilities,
    device_capability_t capability
);

/** @brief Return the canonical contract name, or NULL for an invalid value. */
const char *device_capability_name(device_capability_t capability);

/**
 * @brief Resolve a canonical contract name to its capability value.
 *
 * Returns false and leaves capability_out unchanged when the name is unknown.
 */
bool device_capability_from_name(
    const char *capability_name,
    device_capability_t *capability_out
);

/** @brief Return the removable-module descriptor for device_capabilities. */
const module_descriptor_t *device_capabilities_module_descriptor(void);

#ifdef __cplusplus
}
#endif
