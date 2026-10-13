#include "device_capabilities.h"

device_capability_bitmap_t device_capabilities_get(void) {
    return 0U;
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
