#include "bluetooth_audio_sync_core.h"

#include <string.h>

bool bluetooth_audio_sync_core_parse_codec(
    const char *value,
    bluetooth_codec_t *codec_out
) {
    if (value == NULL || codec_out == NULL) {
        return false;
    }
    if (strcmp(value, "auto") == 0) {
        *codec_out = BLUETOOTH_CODEC_AUTO;
        return true;
    }
    if (strcmp(value, "sbc") == 0) {
        *codec_out = BLUETOOTH_CODEC_SBC;
        return true;
    }
    if (strcmp(value, "aac") == 0) {
        *codec_out = BLUETOOTH_CODEC_AAC;
        return true;
    }
    if (strcmp(value, "ldac") == 0) {
        *codec_out = BLUETOOTH_CODEC_LDAC;
        return true;
    }
    return false;
}

bool bluetooth_audio_sync_core_build_policy(
    const bluetooth_audio_remote_t *remote,
    bluetooth_policy_t *policy_out
) {
    if (remote == NULL || policy_out == NULL) {
        return false;
    }
    bluetooth_codec_t codec = BLUETOOTH_CODEC_AUTO;
    if (!bluetooth_audio_sync_core_parse_codec(
            remote->preferred_codec,
            &codec
        )) {
        return false;
    }
    uint32_t volume = remote->max_volume_percent < 0
        ? 0u
        : (uint32_t)remote->max_volume_percent;
    uint32_t revision = remote->revision < 0
        ? 0u
        : (uint32_t)remote->revision;
    bluetooth_policy_t policy = bluetooth_default();
    policy.enabled = remote->enabled ? 1u : 0u;
    policy.speaker_mode_allowed = remote->speaker_mode_allowed ? 1u : 0u;
    policy.pairing_allowed = remote->pairing_allowed ? 1u : 0u;
    policy.auto_reconnect = remote->auto_reconnect ? 1u : 0u;
    policy.preferred_codec = (uint8_t)codec;
    policy.max_volume_percent = (uint8_t)volume;
    policy.revision = revision;
    if (remote->display_name != NULL) {
        size_t length = strlen(remote->display_name);
        if (length >= BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE) {
            length = BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE - 1u;
        }
        memcpy(policy.display_name, remote->display_name, length);
        policy.display_name[length] = '\0';
    }
    // The clamp also clears the derived speaker/pairing flags when disabled.
    *policy_out = bluetooth_clamp(&policy);
    return true;
}
