#include "volume_control.h"

#include <cassert>
#include <cstdint>
#include <cstring>

// The host store stub keeps a tiny in-memory table so this test can exercise
// first boot without any persisted values.
void volume_test_store_reset(bool ready);

namespace {

void test_error_name_mapping() {
    assert(std::strcmp(
               volume_control_error_name(VOLUME_CONTROL_OK),
               "ok"
           ) == 0);
    assert(std::strcmp(
               volume_control_error_name(VOLUME_CONTROL_ERR_NOT_INITIALIZED),
               "audio_codec_not_initialized"
           ) == 0);
    assert(std::strcmp(
               volume_control_error_name(VOLUME_CONTROL_ERR_INVALID_PERCENT),
               "audio_volume_invalid"
           ) == 0);
    assert(std::strcmp(
               volume_control_error_name(VOLUME_CONTROL_ERR_STORAGE),
               "audio_codec_internal"
           ) == 0);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor = volume_control_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "volume_control") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_set_before_init_is_rejected() {
    assert(!volume_control_is_ready());
    assert(
        volume_control_set_percent(10u) == VOLUME_CONTROL_ERR_NOT_INITIALIZED
    );
    assert(
        volume_control_set_max_percent(10u) ==
        VOLUME_CONTROL_ERR_NOT_INITIALIZED
    );
    assert(volume_control_set_muted(true) == ESP_ERR_INVALID_STATE);
}

void test_first_boot_uses_bounded_default() {
    assert(volume_control_init() == ESP_OK);
    assert(volume_control_is_ready());
    // init is idempotent once ready.
    assert(volume_control_init() == ESP_OK);

    // Compiled default 40 is below the 70 cap, so it is the live volume.
    assert(volume_control_get_percent() == 40u);
    assert(volume_control_get_max_percent() == 70u);
    assert(!volume_control_is_muted());

    const volume_control_snapshot_t snapshot = volume_control_get_snapshot();
    assert(snapshot.volume_percent == 40u);
    assert(snapshot.max_volume_percent == 70u);
    assert(!snapshot.is_muted);
}

void test_set_percent_clamps_to_guardian_max() {
    assert(volume_control_set_percent(50u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_percent() == 50u);

    // A loud request above the cap is clamped, not rejected.
    assert(volume_control_set_percent(100u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_percent() == 70u);

    assert(volume_control_set_percent(0u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_percent() == 0u);
}

void test_lowering_max_reduces_live_volume() {
    assert(volume_control_set_percent(60u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_percent() == 60u);

    assert(volume_control_set_max_percent(30u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_max_percent() == 30u);
    assert(volume_control_get_percent() == 30u);

    // Raising the cap again does not restore the reduced value.
    assert(volume_control_set_max_percent(90u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_percent() == 30u);
}

void test_mute_forces_effective_volume_to_zero() {
    assert(volume_control_set_percent(45u) == VOLUME_CONTROL_OK);

    assert(volume_control_set_muted(true) == ESP_OK);
    assert(volume_control_is_muted());
    assert(volume_control_get_percent() == 0u);

    const volume_control_snapshot_t muted = volume_control_get_snapshot();
    assert(muted.is_muted);
    assert(muted.volume_percent == 0u);

    assert(volume_control_set_muted(false) == ESP_OK);
    assert(!volume_control_is_muted());
    assert(volume_control_get_percent() == 45u);
}

void test_apply_gain_scales_and_saturates() {
    assert(volume_control_set_max_percent(100u) == VOLUME_CONTROL_OK);
    assert(volume_control_set_percent(50u) == VOLUME_CONTROL_OK);

    int16_t frame[] = {100, -100, 32767, -32768, 0};
    const int16_t expected[] = {50, -50, 16383, -16384, 0};
    assert(volume_control_apply_gain(frame, 5) == ESP_OK);
    for (size_t index = 0; index < 5; ++index) {
        assert(frame[index] == expected[index]);
    }

    // Full scale keeps the exact samples.
    assert(volume_control_set_percent(100u) == VOLUME_CONTROL_OK);
    int16_t loud[] = {32767, -32768, 12345};
    assert(volume_control_apply_gain(loud, 3) == ESP_OK);
    assert(loud[0] == 32767);
    assert(loud[1] == -32768);
    assert(loud[2] == 12345);

    // Mute zeroes the whole frame.
    assert(volume_control_set_muted(true) == ESP_OK);
    int16_t quiet[] = {32767, -32768, 100};
    assert(volume_control_apply_gain(quiet, 3) == ESP_OK);
    assert(quiet[0] == 0);
    assert(quiet[1] == 0);
    assert(quiet[2] == 0);
}

void test_apply_gain_argument_guards() {
    assert(volume_control_apply_gain(nullptr, 0) == ESP_OK);
    assert(volume_control_apply_gain(nullptr, 1) == ESP_ERR_INVALID_ARG);
}

}  // namespace

int main() {
    // config_store is ready but empty, so this is a clean first boot.
    volume_test_store_reset(true);

    test_error_name_mapping();
    test_module_descriptor_is_stable();
    test_set_before_init_is_rejected();
    test_first_boot_uses_bounded_default();
    test_set_percent_clamps_to_guardian_max();
    test_lowering_max_reduces_live_volume();
    test_mute_forces_effective_volume_to_zero();
    test_apply_gain_scales_and_saturates();
    test_apply_gain_argument_guards();
    return 0;
}
