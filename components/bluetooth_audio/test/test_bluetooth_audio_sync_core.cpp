#include "bluetooth_audio_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // Every catalog codec maps to its enum; an unknown name is rejected.
    bluetooth_codec_t codec = BLUETOOTH_CODEC_LDAC;
    assert(bluetooth_audio_sync_core_parse_codec("auto", &codec));
    assert(codec == BLUETOOTH_CODEC_AUTO);
    assert(bluetooth_audio_sync_core_parse_codec("sbc", &codec));
    assert(codec == BLUETOOTH_CODEC_SBC);
    assert(bluetooth_audio_sync_core_parse_codec("aac", &codec));
    assert(codec == BLUETOOTH_CODEC_AAC);
    assert(bluetooth_audio_sync_core_parse_codec("ldac", &codec));
    assert(codec == BLUETOOTH_CODEC_LDAC);
    assert(!bluetooth_audio_sync_core_parse_codec("aptx", &codec));
    assert(!bluetooth_audio_sync_core_parse_codec(nullptr, &codec));

    // A fully populated enabled policy is clamped and mapped.
    bluetooth_audio_remote_t remote = {};
    remote.enabled = true;
    remote.speaker_mode_allowed = true;
    remote.pairing_allowed = true;
    remote.auto_reconnect = true;
    remote.preferred_codec = "aac";
    remote.max_volume_percent = 250;
    remote.display_name = "客厅音箱";
    bluetooth_policy_t policy = {};
    assert(bluetooth_audio_sync_core_build_policy(&remote, &policy));
    assert(policy.schema_version == BLUETOOTH_AUDIO_SCHEMA_VERSION);
    assert(policy.enabled == 1u);
    assert(policy.speaker_mode_allowed == 1u);
    assert(policy.pairing_allowed == 1u);
    assert(policy.auto_reconnect == 1u);
    assert(policy.preferred_codec == (uint8_t)BLUETOOTH_CODEC_AAC);
    // The volume ceiling is clamped to the platform maximum.
    assert(policy.max_volume_percent == BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT);
    assert(std::strcmp(policy.display_name, "客厅音箱") == 0);
    assert(bluetooth_is_valid(&policy));

    // A disabled policy clears the derived speaker and pairing flags.
    bluetooth_audio_remote_t disabled = remote;
    disabled.enabled = false;
    disabled.speaker_mode_allowed = true;
    disabled.pairing_allowed = true;
    bluetooth_policy_t disabled_policy = {};
    assert(bluetooth_audio_sync_core_build_policy(&disabled, &disabled_policy));
    assert(disabled_policy.enabled == 0u);
    assert(disabled_policy.speaker_mode_allowed == 0u);
    assert(disabled_policy.pairing_allowed == 0u);
    assert(!bluetooth_can_speaker(&disabled_policy));
    assert(!bluetooth_can_pair(&disabled_policy));

    // A negative volume percent floors at zero rather than wrapping.
    bluetooth_audio_remote_t negative = remote;
    negative.max_volume_percent = -30;
    bluetooth_policy_t negative_policy = {};
    assert(bluetooth_audio_sync_core_build_policy(&negative, &negative_policy));
    assert(negative_policy.max_volume_percent == 0u);

    // An unknown codec rejects the whole policy.
    bluetooth_audio_remote_t unknown = remote;
    unknown.preferred_codec = "aptx";
    bluetooth_policy_t unknown_policy = {};
    assert(!bluetooth_audio_sync_core_build_policy(&unknown, &unknown_policy));

    // A missing display name is treated as an empty, valid name.
    bluetooth_audio_remote_t anonymous = remote;
    anonymous.display_name = nullptr;
    bluetooth_policy_t anonymous_policy = {};
    assert(bluetooth_audio_sync_core_build_policy(&anonymous, &anonymous_policy));
    assert(anonymous_policy.display_name[0] == '\0');

    return 0;
}
