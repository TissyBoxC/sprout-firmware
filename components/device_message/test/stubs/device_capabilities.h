#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVICE_CAPABILITY_CAMERA = 0,
    DEVICE_CAPABILITY_DISPLAY,
    DEVICE_CAPABILITY_TOUCH,
    DEVICE_CAPABILITY_CELLULAR,
    DEVICE_CAPABILITY_BATTERY,
} device_capability_t;

typedef uint32_t device_capability_bitmap_t;

device_capability_bitmap_t device_capabilities_get(void);

bool device_capabilities_has(
    device_capability_bitmap_t bitmap,
    device_capability_t capability
);

#ifdef __cplusplus
}
#endif
