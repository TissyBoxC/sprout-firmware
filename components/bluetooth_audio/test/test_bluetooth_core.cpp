#include "bluetooth_audio.h"

#include <cassert>
#include <cstring>

int main() {
    const bluetooth_policy_t defaults = bluetooth_default();
    assert(bluetooth_is_valid(&defaults));
    assert(defaults.enabled == 0u);
    assert(defaults.pairing_allowed == 0u);
    assert(defaults.speaker_mode_allowed == 1u);
    assert(defaults.auto_reconnect == 1u);
    assert(defaults.preferred_codec == (uint8_t)BLUETOOTH_CODEC_AUTO);
    assert(defaults.max_volume_percent == 70u);
    // The default policy is opt-in, so it cannot speaker or pair yet.
    assert(!bluetooth_can_speaker(&defaults));
    assert(!bluetooth_can_pair(&defaults));

    bluetooth_policy_t out_of_range = defaults;
    out_of_range.enabled = 1u;
    out_of_range.preferred_codec = 200u;
    out_of_range.max_volume_percent = 250u;
    const bluetooth_policy_t clamped = bluetooth_clamp(&out_of_range);
    assert(clamped.preferred_codec == (uint8_t)BLUETOOTH_CODEC_LDAC);
    assert(clamped.max_volume_percent == BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT);
    assert(clamped.enabled == 1u);
    assert(bluetooth_is_valid(&clamped));
    // An enabled policy that allows both derived flags can speaker and pair.
    assert(bluetooth_can_speaker(&clamped));

    // Disabling clamps the derived flags off even when the caller left them set.
    bluetooth_policy_t disabled = clamped;
    disabled.enabled = 0u;
    disabled.speaker_mode_allowed = 1u;
    disabled.pairing_allowed = 1u;
    const bluetooth_policy_t disabled_clamped = bluetooth_clamp(&disabled);
    assert(disabled_clamped.speaker_mode_allowed == 0u);
    assert(disabled_clamped.pairing_allowed == 0u);
    assert(!bluetooth_can_speaker(&disabled_clamped));
    assert(!bluetooth_can_pair(&disabled_clamped));

    assert(!bluetooth_is_valid(nullptr));
    bluetooth_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!bluetooth_is_valid(&wrong_version));
    assert(bluetooth_clamp(nullptr).preferred_codec ==
           (uint8_t)BLUETOOTH_CODEC_AUTO);

    // An over-long display name is truncated to the bounded buffer.
    bluetooth_policy_t long_name = defaults;
    std::memset(long_name.display_name, 'x', sizeof(long_name.display_name));
    const bluetooth_policy_t truncated = bluetooth_clamp(&long_name);
    assert(truncated.display_name[BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE - 1u] ==
           '\0');
    assert(std::strlen(truncated.display_name) ==
           BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE - 1u);

    // Catalog membership is bounded on both ends.
    assert(bluetooth_is_valid_codec(BLUETOOTH_CODEC_AUTO));
    assert(bluetooth_is_valid_codec(BLUETOOTH_CODEC_LDAC));
    assert(!bluetooth_is_valid_codec((bluetooth_codec_t)99));
    assert(bluetooth_is_valid_active_codec(BLUETOOTH_ACTIVE_CODEC_UNKNOWN));
    assert(bluetooth_is_valid_active_codec(BLUETOOTH_ACTIVE_CODEC_LDAC));
    assert(!bluetooth_is_valid_active_codec((bluetooth_active_codec_t)99));
    assert(bluetooth_is_valid_adapter_state(BLUETOOTH_ADAPTER_OFF));
    assert(bluetooth_is_valid_adapter_state(BLUETOOTH_ADAPTER_CONNECTED));
    assert(!bluetooth_is_valid_adapter_state((bluetooth_adapter_state_t)99));
    assert(bluetooth_is_valid_disconnect_reason(BLUETOOTH_DISCONNECT_NONE));
    assert(bluetooth_is_valid_disconnect_reason(BLUETOOTH_DISCONNECT_ERROR));
    assert(!bluetooth_is_valid_disconnect_reason(
        (bluetooth_disconnect_reason_t)99
    ));

    // Stable names round-trip for the platform contract.
    assert(std::strcmp(bluetooth_adapter_state_name(BLUETOOTH_ADAPTER_OFF),
                       "off") == 0);
    assert(std::strcmp(
               bluetooth_adapter_state_name(BLUETOOTH_ADAPTER_ADVERTISING),
               "advertising"
           ) == 0);
    assert(std::strcmp(
               bluetooth_disconnect_reason_name(BLUETOOTH_DISCONNECT_POLICY),
               "policy"
           ) == 0);
    assert(std::strcmp(bluetooth_active_codec_name(
                           BLUETOOTH_ACTIVE_CODEC_UNKNOWN),
                       "unknown") == 0);
    assert(std::strcmp(bluetooth_codec_name(BLUETOOTH_CODEC_AUTO), "auto") == 0);
    return 0;
}
