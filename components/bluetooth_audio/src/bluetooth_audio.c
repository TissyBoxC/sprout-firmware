#include "bluetooth_audio.h"

#include <stddef.h>

#include "config_store.h"
#include "bluetooth_audio_sync_core.h"

// One bounded binary blob persists the guardian Bluetooth policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define BLUETOOTH_AUDIO_STORE_KEY "bt_policy"
CONFIG_STORE_KEY_ASSERT(BLUETOOTH_AUDIO_STORE_KEY);

static bluetooth_policy_t s_policy;
static bluetooth_adapter_state_t s_adapter_state;
static bluetooth_active_codec_t s_active_codec;
static bluetooth_disconnect_reason_t s_disconnect_reason;
static uint8_t s_volume_percent;
static uint8_t s_paired_device_count;
static uint8_t s_connected_device_count;
static bool s_speaker_mode;
static bool s_ready;

static esp_err_t bluetooth_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        BLUETOOTH_AUDIO_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return BLUETOOTH_AUDIO_ERR_STORAGE;
    }
    return ESP_OK;
}

static void bluetooth_restore(void) {
    bluetooth_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        BLUETOOTH_AUDIO_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        bluetooth_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = bluetooth_default();
}

static void bluetooth_reconcile_runtime(void) {
    // The adapter only runs while the feature is enabled; a disabled policy
    // always parks the adapter off with no speaker, codec or volume leak.
    if (s_policy.enabled == 0u) {
        s_adapter_state = BLUETOOTH_ADAPTER_OFF;
        s_active_codec = BLUETOOTH_ACTIVE_CODEC_UNKNOWN;
        s_speaker_mode = false;
        s_volume_percent = 0u;
        return;
    }
    if (s_adapter_state == BLUETOOTH_ADAPTER_OFF) {
        s_adapter_state = BLUETOOTH_ADAPTER_IDLE;
    }
    if (s_adapter_state != BLUETOOTH_ADAPTER_CONNECTED) {
        s_active_codec = BLUETOOTH_ACTIVE_CODEC_UNKNOWN;
        s_speaker_mode = false;
    }
}

esp_err_t bluetooth_audio_init(void) {
    bluetooth_restore();
    s_adapter_state = s_policy.enabled != 0u ? BLUETOOTH_ADAPTER_IDLE
                                             : BLUETOOTH_ADAPTER_OFF;
    s_active_codec = BLUETOOTH_ACTIVE_CODEC_UNKNOWN;
    s_disconnect_reason = BLUETOOTH_DISCONNECT_NONE;
    s_volume_percent = 0u;
    s_paired_device_count = 0u;
    s_connected_device_count = 0u;
    s_speaker_mode = false;
    s_ready = true;
    return ESP_OK;
}

bool bluetooth_audio_is_ready(void) {
    return s_ready;
}

esp_err_t bluetooth_audio_apply(const bluetooth_policy_t *policy) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (policy == NULL || !bluetooth_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    bluetooth_reconcile_runtime();
    return ESP_OK;
}

esp_err_t bluetooth_audio_set(const bluetooth_policy_t *policy) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = bluetooth_clamp(policy);
    bluetooth_reconcile_runtime();
    return bluetooth_persist();
}

esp_err_t bluetooth_audio_apply_remote(
    const bluetooth_audio_remote_t *remote
) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    bluetooth_policy_t policy = {};
    if (!bluetooth_audio_sync_core_build_policy(remote, &policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    return bluetooth_audio_set(&policy);
}

bluetooth_policy_t bluetooth_audio_get(void) {
    return s_policy;
}

esp_err_t bluetooth_audio_set_enabled(bool enabled) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    s_policy.enabled = enabled ? 1u : 0u;
    if (!enabled) {
        // Disabling revokes the derived permissions so a stale speaker control
        // can never survive the switch.
        s_policy.speaker_mode_allowed = 0u;
        s_policy.pairing_allowed = 0u;
        s_disconnect_reason = BLUETOOTH_DISCONNECT_POLICY;
    }
    bluetooth_reconcile_runtime();
    return bluetooth_persist();
}

esp_err_t bluetooth_audio_start_pairing(void) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    // The policy gate runs first so a disabled or unauthorised policy reports a
    // permission error rather than a misleading adapter-state error.
    if (!bluetooth_can_pair(&s_policy)) {
        return BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED;
    }
    if (s_adapter_state != BLUETOOTH_ADAPTER_IDLE) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    s_disconnect_reason = BLUETOOTH_DISCONNECT_NONE;
    s_adapter_state = BLUETOOTH_ADAPTER_ADVERTISING;
    return ESP_OK;
}

esp_err_t bluetooth_audio_connect(bluetooth_active_codec_t codec) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (s_policy.enabled == 0u) {
        return BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED;
    }
    if (!bluetooth_is_valid_active_codec(codec) ||
        codec == BLUETOOTH_ACTIVE_CODEC_UNKNOWN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adapter_state != BLUETOOTH_ADAPTER_IDLE &&
        s_adapter_state != BLUETOOTH_ADAPTER_ADVERTISING) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    s_adapter_state = BLUETOOTH_ADAPTER_CONNECTING;
    s_adapter_state = BLUETOOTH_ADAPTER_CONNECTED;
    s_active_codec = codec;
    s_speaker_mode = false;
    s_disconnect_reason = BLUETOOTH_DISCONNECT_NONE;
    return ESP_OK;
}

esp_err_t bluetooth_audio_disconnect(bluetooth_disconnect_reason_t reason) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (!bluetooth_is_valid_disconnect_reason(reason)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adapter_state != BLUETOOTH_ADAPTER_CONNECTED &&
        s_adapter_state != BLUETOOTH_ADAPTER_CONNECTING &&
        s_adapter_state != BLUETOOTH_ADAPTER_ADVERTISING) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    // Leaving the connected state always clears speaker mode and the active
    // codec so a stale link never reads as an active speaker.
    s_adapter_state = s_policy.enabled != 0u ? BLUETOOTH_ADAPTER_IDLE
                                             : BLUETOOTH_ADAPTER_OFF;
    s_active_codec = BLUETOOTH_ACTIVE_CODEC_UNKNOWN;
    s_speaker_mode = false;
    s_disconnect_reason = reason;
    return ESP_OK;
}

esp_err_t bluetooth_audio_set_speaker_mode(bool enabled) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (!enabled) {
        s_speaker_mode = false;
        return ESP_OK;
    }
    if (s_adapter_state != BLUETOOTH_ADAPTER_CONNECTED) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (!bluetooth_can_speaker(&s_policy)) {
        return BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED;
    }
    s_speaker_mode = true;
    return ESP_OK;
}

esp_err_t bluetooth_audio_report_adapter_state(
    bluetooth_adapter_state_t state
) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    if (!bluetooth_is_valid_adapter_state(state)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_adapter_state = state;
    if (state != BLUETOOTH_ADAPTER_CONNECTED) {
        s_speaker_mode = false;
        if (state == BLUETOOTH_ADAPTER_OFF) {
            s_active_codec = BLUETOOTH_ACTIVE_CODEC_UNKNOWN;
            s_volume_percent = 0u;
        }
    }
    return ESP_OK;
}

esp_err_t bluetooth_audio_report_volume(uint8_t volume_percent) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    uint32_t bounded = volume_percent;
    if (bounded > BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT) {
        bounded = BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT;
    }
    // The guardian ceiling always wins, so a device volume report can never
    // exceed the policy even if the local control was raised.
    if (bounded > s_policy.max_volume_percent) {
        bounded = s_policy.max_volume_percent;
    }
    s_volume_percent = (uint8_t)bounded;
    return ESP_OK;
}

bluetooth_adapter_state_t bluetooth_audio_adapter_state(void) {
    return s_adapter_state;
}

esp_err_t bluetooth_audio_report_link_counts(
    uint8_t paired_device_count,
    uint8_t connected_device_count
) {
    if (!s_ready) {
        return BLUETOOTH_AUDIO_ERR_INVALID_STATE;
    }
    uint32_t paired = paired_device_count;
    if (paired > BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES) {
        paired = BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES;
    }
    uint32_t connected = connected_device_count;
    if (connected > BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES) {
        connected = BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES;
    }
    // The platform rejects a connected count above the paired count, so the
    // pair is reconciled here before it can reach a status report.
    if (connected > paired) {
        connected = paired;
    }
    s_paired_device_count = (uint8_t)paired;
    s_connected_device_count = (uint8_t)connected;
    return ESP_OK;
}

bluetooth_active_codec_t bluetooth_audio_active_codec(void) {
    return s_active_codec;
}

bluetooth_disconnect_reason_t bluetooth_audio_last_disconnect_reason(void) {
    return s_disconnect_reason;
}

uint8_t bluetooth_audio_volume_percent(void) {
    return s_volume_percent;
}

bool bluetooth_audio_speaker_mode(void) {
    return s_speaker_mode;
}

bluetooth_audio_status_t bluetooth_audio_status(void) {
    bluetooth_audio_status_t status = {
        .supported = true,
        .speaker_mode = s_speaker_mode,
        .adapter_state = s_adapter_state,
        .paired_device_count = s_paired_device_count,
        .connected_device_count = s_connected_device_count,
        .active_codec = s_active_codec,
        .volume_percent = s_volume_percent,
        .last_disconnect_reason = s_disconnect_reason,
        .revision = s_policy.revision,
    };
    // A disabled policy reports an off adapter with no live link, so a stale
    // state never reads as an active speaker on the platform.
    if (s_policy.enabled == 0u) {
        status.adapter_state = BLUETOOTH_ADAPTER_OFF;
        status.active_codec = BLUETOOTH_ACTIVE_CODEC_UNKNOWN;
        status.speaker_mode = false;
        status.paired_device_count = 0u;
        status.connected_device_count = 0u;
        status.volume_percent = 0u;
    }
    return status;
}

static esp_err_t bluetooth_audio_initialize(void) {
    return bluetooth_audio_init();
}

const module_descriptor_t *bluetooth_audio_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "bluetooth_audio",
        .version = "1.0.0",
        .initialize = bluetooth_audio_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
