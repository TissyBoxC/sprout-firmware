#include "device_capabilities.h"

device_capability_bitmap_t device_capabilities_get(void) {
    return (device_capability_bitmap_t)(1u << DEVICE_CAPABILITY_DISPLAY);
}

bool device_capabilities_has(
    device_capability_bitmap_t bitmap,
    device_capability_t capability
) {
    return (bitmap & (device_capability_bitmap_t)(1u << capability)) != 0u;
}
