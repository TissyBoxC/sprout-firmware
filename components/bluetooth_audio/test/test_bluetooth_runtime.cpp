#include "bluetooth_audio.h"

#include <cassert>
#include <cstring>

int main() {
    assert(bluetooth_audio_init() == ESP_OK);
    assert(bluetooth_audio_is_ready());
    // The default policy disables the feature, so the adapter parks off.
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_OFF);
    assert(!bluetooth_audio_speaker_mode());
    assert(bluetooth_audio_volume_percent() == 0u);

    // Pairing is refused while the feature is disabled.
    assert(bluetooth_audio_start_pairing() ==
           BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED);

    // Enable the feature and authorise pairing.
    bluetooth_policy_t policy = bluetooth_audio_get();
    policy.enabled = 1u;
    policy.speaker_mode_allowed = 1u;
    policy.pairing_allowed = 1u;
    assert(bluetooth_audio_set(&policy) == ESP_OK);
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_IDLE);

    assert(bluetooth_audio_start_pairing() == ESP_OK);
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_ADVERTISING);

    // Connecting resolves a bounded active codec.
    assert(bluetooth_audio_connect(BLUETOOTH_ACTIVE_CODEC_SBC) == ESP_OK);
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_CONNECTED);
    assert(bluetooth_audio_active_codec() == BLUETOOTH_ACTIVE_CODEC_SBC);

    // The unknown codec is never accepted as an active link.
    assert(bluetooth_audio_connect(BLUETOOTH_ACTIVE_CODEC_UNKNOWN) != ESP_OK);

    // Speaker mode requires a live connection and the authorised policy.
    assert(bluetooth_audio_set_speaker_mode(true) == ESP_OK);
    assert(bluetooth_audio_speaker_mode());

    // The volume report is clamped to the policy ceiling.
    assert(bluetooth_audio_report_volume(250u) == ESP_OK);
    assert(bluetooth_audio_volume_percent() ==
           bluetooth_audio_get().max_volume_percent);

    // A disconnect clears speaker mode and the active codec.
    assert(bluetooth_audio_disconnect(BLUETOOTH_DISCONNECT_USER) == ESP_OK);
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_IDLE);
    assert(!bluetooth_audio_speaker_mode());
    assert(bluetooth_audio_active_codec() == BLUETOOTH_ACTIVE_CODEC_UNKNOWN);
    assert(bluetooth_audio_last_disconnect_reason() ==
           BLUETOOTH_DISCONNECT_USER);

    // Link counts are clamped and a connected count never exceeds the paired
    // count, so a status report is always accepted by the platform.
    assert(bluetooth_audio_report_link_counts(3u, 2u) == ESP_OK);
    assert(bluetooth_audio_report_link_counts(2u, 5u) == ESP_OK);
    assert(bluetooth_audio_report_link_counts(99u, 99u) == ESP_OK);
    bluetooth_audio_status_t saturated = bluetooth_audio_status();
    assert(saturated.paired_device_count ==
           BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES);
    assert(saturated.connected_device_count ==
           BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES);

    // The status snapshot mirrors the live adapter state and policy revision.
    bluetooth_policy_t revisioned = bluetooth_audio_get();
    revisioned.enabled = 1u;
    revisioned.pairing_allowed = 1u;
    revisioned.revision = 12u;
    assert(bluetooth_audio_set(&revisioned) == ESP_OK);
    assert(bluetooth_audio_report_link_counts(4u, 1u) == ESP_OK);
    assert(bluetooth_audio_connect(BLUETOOTH_ACTIVE_CODEC_LDAC) == ESP_OK);
    assert(bluetooth_audio_set_speaker_mode(true) == ESP_OK);
    assert(bluetooth_audio_report_volume(30u) == ESP_OK);
    bluetooth_audio_status_t status = bluetooth_audio_status();
    assert(status.supported);
    assert(status.adapter_state == BLUETOOTH_ADAPTER_CONNECTED);
    assert(status.active_codec == BLUETOOTH_ACTIVE_CODEC_LDAC);
    assert(status.speaker_mode);
    assert(status.paired_device_count == 4u);
    assert(status.connected_device_count == 1u);
    assert(status.volume_percent == 30u);
    assert(status.revision == 12u);

    // Disabling the feature revokes speaker mode and pairing in one step.
    assert(bluetooth_audio_set_enabled(false) == ESP_OK);
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_OFF);
    assert(bluetooth_audio_get().enabled == 0u);
    assert(bluetooth_audio_get().speaker_mode_allowed == 0u);
    assert(bluetooth_audio_get().pairing_allowed == 0u);
    // A disabled policy reports a fully off status with no live link.
    bluetooth_audio_status_t disabled = bluetooth_audio_status();
    assert(!disabled.speaker_mode);
    assert(disabled.adapter_state == BLUETOOTH_ADAPTER_OFF);
    assert(disabled.active_codec == BLUETOOTH_ACTIVE_CODEC_UNKNOWN);
    assert(disabled.paired_device_count == 0u);
    assert(disabled.connected_device_count == 0u);
    assert(disabled.volume_percent == 0u);
    assert(bluetooth_audio_start_pairing() ==
           BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED);
    assert(bluetooth_audio_connect(BLUETOOTH_ACTIVE_CODEC_AAC) ==
           BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED);

    // A speaker-mode request while disconnected is refused.
    assert(bluetooth_audio_set_speaker_mode(true) ==
           BLUETOOTH_AUDIO_ERR_INVALID_STATE);

    // Persistence round trip keeps the edited policy.
    bluetooth_policy_t persisted = bluetooth_default();
    persisted.enabled = 1u;
    persisted.pairing_allowed = 1u;
    persisted.preferred_codec = (uint8_t)BLUETOOTH_CODEC_AAC;
    persisted.max_volume_percent = 40u;
    std::strncpy(persisted.display_name, "Kitchen", sizeof(persisted.display_name) - 1u);
    assert(bluetooth_audio_set(&persisted) == ESP_OK);
    assert(bluetooth_audio_init() == ESP_OK);
    assert(bluetooth_audio_get().enabled == 1u);
    assert(bluetooth_audio_get().preferred_codec ==
           (uint8_t)BLUETOOTH_CODEC_AAC);
    assert(bluetooth_audio_get().max_volume_percent == 40u);
    assert(std::strcmp(bluetooth_audio_get().display_name, "Kitchen") == 0);

    // A policy that is not version-compatible is refused by apply.
    bluetooth_policy_t invalid = bluetooth_default();
    invalid.schema_version = 42u;
    assert(bluetooth_audio_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(bluetooth_audio_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = bluetooth_audio_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
