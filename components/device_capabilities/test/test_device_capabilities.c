#include "device_capabilities.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void test_contract_version(void) {
    assert(strcmp(device_capabilities_contract_version(), "1.0.0") == 0);
}

static void test_every_capability_has_a_unique_name(void) {
    // The names and ordering are the shared 1.0.0 contract, so every index must
    // resolve to a non-null name and no two may collide.
    int raw;
    for (raw = 0; raw < DEVICE_CAPABILITY_COUNT; ++raw) {
        const char *name =
            device_capability_name((device_capability_t)raw);
        int other;
        assert(name != NULL);
        assert(name[0] != '\0');
        for (other = raw + 1; other < DEVICE_CAPABILITY_COUNT; ++other) {
            assert(
                strcmp(name, device_capability_name((device_capability_t)other))
                    != 0
            );
        }
    }
}

static void test_known_names_are_stable(void) {
    assert(
        strcmp(
            device_capability_name(DEVICE_CAPABILITY_AUDIO_INPUT),
            "audio_input"
        ) == 0
    );
    assert(
        strcmp(
            device_capability_name(DEVICE_CAPABILITY_CELLULAR_4G),
            "cellular_4g"
        ) == 0
    );
    assert(
        strcmp(
            device_capability_name(DEVICE_CAPABILITY_MULTI_DEVICE),
            "multi_device"
        ) == 0
    );
}

static void test_invalid_indices_are_rejected(void) {
    assert(
        device_capability_name((device_capability_t)-1) == NULL
    );
    assert(
        device_capability_name((device_capability_t)DEVICE_CAPABILITY_COUNT)
            == NULL
    );
    assert(!device_capabilities_has(0xFFFFFFFFu, (device_capability_t)-1));
    assert(
        !device_capabilities_has(
            0xFFFFFFFFu,
            (device_capability_t)DEVICE_CAPABILITY_COUNT
        )
    );
}

static void test_has_reads_the_right_bit(void) {
    const device_capability_bitmap_t bitmap =
        (1U << DEVICE_CAPABILITY_WIFI) | (1U << DEVICE_CAPABILITY_CAMERA);
    assert(device_capabilities_has(bitmap, DEVICE_CAPABILITY_WIFI));
    assert(device_capabilities_has(bitmap, DEVICE_CAPABILITY_CAMERA));
    assert(!device_capabilities_has(bitmap, DEVICE_CAPABILITY_TOUCH));
    assert(!device_capabilities_has(0u, DEVICE_CAPABILITY_WIFI));
}

static void test_name_round_trip(void) {
    int raw;
    for (raw = 0; raw < DEVICE_CAPABILITY_COUNT; ++raw) {
        const device_capability_t capability = (device_capability_t)raw;
        device_capability_t resolved = DEVICE_CAPABILITY_COUNT;
        assert(
            device_capability_from_name(
                device_capability_name(capability),
                &resolved
            )
        );
        assert(resolved == capability);
    }
}

static void test_from_name_rejects_bad_input(void) {
    device_capability_t resolved = DEVICE_CAPABILITY_WIFI;
    // Unknown names must fail and leave the output untouched.
    assert(!device_capability_from_name("teleport", &resolved));
    assert(resolved == DEVICE_CAPABILITY_WIFI);
    assert(!device_capability_from_name(NULL, &resolved));
    assert(resolved == DEVICE_CAPABILITY_WIFI);
    assert(!device_capability_from_name("wifi", NULL));
}

static void test_configured_capabilities_are_non_empty(void) {
    const module_descriptor_t *descriptor;

    // The build wires these flags, so get() must advertise at least one bit.
    assert(device_capabilities_get() != 0u);

    descriptor = device_capabilities_module_descriptor();
    assert(descriptor != NULL);
    assert(descriptor->initialize != NULL);
    assert(descriptor->initialize() == ESP_OK);
    assert(strcmp(descriptor->module_name, "device_capabilities") == 0);
    assert(descriptor->shutdown == NULL);
}

int main(void) {
    test_contract_version();
    test_every_capability_has_a_unique_name();
    test_known_names_are_stable();
    test_invalid_indices_are_rejected();
    test_has_reads_the_right_bit();
    test_name_round_trip();
    test_from_name_rejects_bad_input();
    test_configured_capabilities_are_non_empty();
    return 0;
}
