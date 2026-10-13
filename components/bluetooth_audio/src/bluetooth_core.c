#include "bluetooth_audio.h"

#include <stddef.h>
#include <string.h>

static uint8_t clamp_u8(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) {
        return (uint8_t)minimum;
    }
    if (value > maximum) {
        return (uint8_t)maximum;
    }
    return (uint8_t)value;
}

static void copy_display_name(
    char destination[BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE],
    const char *source
) {
    // The backend bounds the name by rune count and the firmware by bytes; a
    // truncated multi-byte name is acceptable because the platform re-validates
    // the applied policy and the name is display-only.
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t length = strlen(source);
    if (length >= BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE) {
        length = BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE - 1u;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

bluetooth_policy_t bluetooth_default(void) {
    const bluetooth_policy_t policy = {
        .schema_version = BLUETOOTH_AUDIO_SCHEMA_VERSION,
        // Bluetooth audio and pairing start off so the feature is opt-in; the
        // speaker flag is allowed but only takes effect once enabled.
        .enabled = 0u,
        .speaker_mode_allowed = 1u,
        .pairing_allowed = 0u,
        .auto_reconnect = 1u,
        .preferred_codec = (uint8_t)BLUETOOTH_CODEC_AUTO,
        .max_volume_percent = 70u,
        .reserved = 0u,
        .revision = 0u,
        .display_name = "",
    };
    return policy;
}

bluetooth_policy_t bluetooth_clamp(const bluetooth_policy_t *policy) {
    bluetooth_policy_t clamped = bluetooth_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = BLUETOOTH_AUDIO_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.speaker_mode_allowed =
        policy->speaker_mode_allowed != 0u ? 1u : 0u;
    clamped.pairing_allowed = policy->pairing_allowed != 0u ? 1u : 0u;
    clamped.auto_reconnect = policy->auto_reconnect != 0u ? 1u : 0u;
    clamped.preferred_codec = clamp_u8(
        policy->preferred_codec,
        (uint32_t)BLUETOOTH_CODEC_AUTO,
        (uint32_t)BLUETOOTH_CODEC_LDAC
    );
    clamped.max_volume_percent = clamp_u8(
        policy->max_volume_percent,
        0u,
        BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT
    );
    clamped.reserved = 0u;
    clamped.revision = policy->revision;
    copy_display_name(clamped.display_name, policy->display_name);
    // A device cannot advertise as a speaker while the feature is off; keeping
    // the derived flags false here prevents a stale speaker control.
    if (clamped.enabled == 0u) {
        clamped.speaker_mode_allowed = 0u;
        clamped.pairing_allowed = 0u;
    }
    return clamped;
}

bool bluetooth_is_valid(const bluetooth_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != BLUETOOTH_AUDIO_SCHEMA_VERSION) {
        return false;
    }
    if (policy->preferred_codec > (uint8_t)BLUETOOTH_CODEC_LDAC) {
        return false;
    }
    if (policy->max_volume_percent > BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT) {
        return false;
    }
    // The terminator must be present, otherwise a stored blob would be applied
    // with an unterminated display name.
    if (memchr(
            policy->display_name,
            '\0',
            BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE
        ) == NULL) {
        return false;
    }
    return true;
}

bool bluetooth_can_speaker(const bluetooth_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const bluetooth_policy_t bounded = bluetooth_clamp(policy);
    return bounded.enabled != 0u && bounded.speaker_mode_allowed != 0u;
}

bool bluetooth_can_pair(const bluetooth_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const bluetooth_policy_t bounded = bluetooth_clamp(policy);
    return bounded.enabled != 0u && bounded.pairing_allowed != 0u;
}

bool bluetooth_is_valid_codec(bluetooth_codec_t codec) {
    return codec >= BLUETOOTH_CODEC_AUTO && codec <= BLUETOOTH_CODEC_LDAC;
}

bool bluetooth_is_valid_active_codec(bluetooth_active_codec_t codec) {
    return codec >= BLUETOOTH_ACTIVE_CODEC_UNKNOWN &&
           codec <= BLUETOOTH_ACTIVE_CODEC_LDAC;
}

bool bluetooth_is_valid_adapter_state(bluetooth_adapter_state_t state) {
    return state >= BLUETOOTH_ADAPTER_OFF && state <= BLUETOOTH_ADAPTER_ERROR;
}

bool bluetooth_is_valid_disconnect_reason(
    bluetooth_disconnect_reason_t reason
) {
    return reason >= BLUETOOTH_DISCONNECT_NONE &&
           reason <= BLUETOOTH_DISCONNECT_ERROR;
}

const char *bluetooth_error_name(bluetooth_error_t error) {
    switch (error) {
        case BLUETOOTH_AUDIO_OK:
            return "ok";
        case BLUETOOTH_AUDIO_ERR_NULL_ARGUMENT:
            return "null_argument";
        case BLUETOOTH_AUDIO_ERR_INVALID_POLICY:
            return "invalid_policy";
        case BLUETOOTH_AUDIO_ERR_INVALID_STATE:
            return "invalid_state";
        case BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED:
            return "permission_denied";
        case BLUETOOTH_AUDIO_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

const char *bluetooth_codec_name(bluetooth_codec_t codec) {
    switch (codec) {
        case BLUETOOTH_CODEC_AUTO:
            return "auto";
        case BLUETOOTH_CODEC_SBC:
            return "sbc";
        case BLUETOOTH_CODEC_AAC:
            return "aac";
        case BLUETOOTH_CODEC_LDAC:
            return "ldac";
        default:
            return "unknown";
    }
}

const char *bluetooth_active_codec_name(bluetooth_active_codec_t codec) {
    switch (codec) {
        case BLUETOOTH_ACTIVE_CODEC_SBC:
            return "sbc";
        case BLUETOOTH_ACTIVE_CODEC_AAC:
            return "aac";
        case BLUETOOTH_ACTIVE_CODEC_LDAC:
            return "ldac";
        case BLUETOOTH_ACTIVE_CODEC_UNKNOWN:
        default:
            return "unknown";
    }
}

const char *bluetooth_adapter_state_name(bluetooth_adapter_state_t state) {
    switch (state) {
        case BLUETOOTH_ADAPTER_OFF:
            return "off";
        case BLUETOOTH_ADAPTER_IDLE:
            return "idle";
        case BLUETOOTH_ADAPTER_ADVERTISING:
            return "advertising";
        case BLUETOOTH_ADAPTER_CONNECTING:
            return "connecting";
        case BLUETOOTH_ADAPTER_CONNECTED:
            return "connected";
        case BLUETOOTH_ADAPTER_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

const char *bluetooth_disconnect_reason_name(
    bluetooth_disconnect_reason_t reason
) {
    switch (reason) {
        case BLUETOOTH_DISCONNECT_NONE:
            return "none";
        case BLUETOOTH_DISCONNECT_USER:
            return "user";
        case BLUETOOTH_DISCONNECT_RANGE:
            return "range";
        case BLUETOOTH_DISCONNECT_PEER:
            return "peer";
        case BLUETOOTH_DISCONNECT_POLICY:
            return "policy";
        case BLUETOOTH_DISCONNECT_ERROR:
            return "error";
        default:
            return "unknown";
    }
}
