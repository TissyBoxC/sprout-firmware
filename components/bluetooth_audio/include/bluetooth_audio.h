#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable Bluetooth audio schema version shared with the platform. */
#define BLUETOOTH_AUDIO_SCHEMA_VERSION 1u

/** @brief Size of a display name including the terminator. */
#define BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE 49

/** @brief Approved bounds, mirroring the platform contract exactly. */
#define BLUETOOTH_AUDIO_MAX_PAIRED_DEVICES 16
#define BLUETOOTH_AUDIO_MAX_VOLUME_PERCENT 100u

/** @brief The bounded preferred codec catalog shared with the platform. */
typedef enum {
    BLUETOOTH_CODEC_AUTO = 0,
    BLUETOOTH_CODEC_SBC,
    BLUETOOTH_CODEC_AAC,
    BLUETOOTH_CODEC_LDAC,
} bluetooth_codec_t;

/**
 * @brief The bounded active codec catalog shared with the platform.
 *
 * Unlike the preferred codec, an active codec is unknown until a link is
 * established.
 */
typedef enum {
    BLUETOOTH_ACTIVE_CODEC_UNKNOWN = 0,
    BLUETOOTH_ACTIVE_CODEC_SBC,
    BLUETOOTH_ACTIVE_CODEC_AAC,
    BLUETOOTH_ACTIVE_CODEC_LDAC,
} bluetooth_active_codec_t;

/** @brief The normalized Bluetooth adapter states shared with the platform. */
typedef enum {
    BLUETOOTH_ADAPTER_OFF = 0,
    BLUETOOTH_ADAPTER_IDLE,
    BLUETOOTH_ADAPTER_ADVERTISING,
    BLUETOOTH_ADAPTER_CONNECTING,
    BLUETOOTH_ADAPTER_CONNECTED,
    BLUETOOTH_ADAPTER_ERROR,
} bluetooth_adapter_state_t;

/** @brief The stable disconnect reasons shared with the platform. */
typedef enum {
    BLUETOOTH_DISCONNECT_NONE = 0,
    BLUETOOTH_DISCONNECT_USER,
    BLUETOOTH_DISCONNECT_RANGE,
    BLUETOOTH_DISCONNECT_PEER,
    BLUETOOTH_DISCONNECT_POLICY,
    BLUETOOTH_DISCONNECT_ERROR,
} bluetooth_disconnect_reason_t;

/**
 * @brief One bounded Bluetooth audio policy for a family.
 *
 * The struct mirrors the platform policy field for field. Bluetooth audio and
 * pairing default to off so a device never advertises or accepts a pairing
 * without an explicit guardian decision.
 */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t speaker_mode_allowed;
    uint8_t pairing_allowed;
    uint8_t auto_reconnect;
    uint8_t preferred_codec;
    uint8_t max_volume_percent;
    uint8_t reserved;
    char display_name[BLUETOOTH_AUDIO_DISPLAY_NAME_SIZE];
} bluetooth_policy_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    BLUETOOTH_AUDIO_OK = 0,
    BLUETOOTH_AUDIO_ERR_NULL_ARGUMENT,
    BLUETOOTH_AUDIO_ERR_INVALID_POLICY,
    BLUETOOTH_AUDIO_ERR_INVALID_STATE,
    BLUETOOTH_AUDIO_ERR_PERMISSION_DENIED,
    BLUETOOTH_AUDIO_ERR_STORAGE,
} bluetooth_error_t;

/**
 * @brief One guardian policy as carried by the device endpoint.
 *
 * The codec is a catalog name ("auto", "sbc", "aac", "ldac") rather than the
 * firmware enum so the network layer can populate it without knowing the
 * firmware catalog. The strings are owned by the caller.
 */
typedef struct {
    bool enabled;
    bool speaker_mode_allowed;
    bool pairing_allowed;
    bool auto_reconnect;
    const char *preferred_codec;
    int max_volume_percent;
    const char *display_name;
} bluetooth_audio_remote_t;

/** @brief Return the conservative default policy (Bluetooth audio off). */
bluetooth_policy_t bluetooth_default(void);

/** @brief Clamp one policy into the documented bounds. */
bluetooth_policy_t bluetooth_clamp(const bluetooth_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool bluetooth_is_valid(const bluetooth_policy_t *policy);

/** @brief Return true when the policy permits acting as a speaker. */
bool bluetooth_can_speaker(const bluetooth_policy_t *policy);

/** @brief Return true when the policy permits accepting a new pairing. */
bool bluetooth_can_pair(const bluetooth_policy_t *policy);

/** @brief Return true when a preferred codec value is in the catalog. */
bool bluetooth_is_valid_codec(bluetooth_codec_t codec);

/** @brief Return true when an active codec value is in the catalog. */
bool bluetooth_is_valid_active_codec(bluetooth_active_codec_t codec);

/** @brief Return true when an adapter state value is in the catalog. */
bool bluetooth_is_valid_adapter_state(bluetooth_adapter_state_t state);

/** @brief Return true when a disconnect reason is in the catalog. */
bool bluetooth_is_valid_disconnect_reason(bluetooth_disconnect_reason_t reason);

/** @brief Return the stable string for one Bluetooth error code. */
const char *bluetooth_error_name(bluetooth_error_t error);

/** @brief Return the stable string for one preferred codec. */
const char *bluetooth_codec_name(bluetooth_codec_t codec);

/** @brief Return the stable string for one active codec. */
const char *bluetooth_active_codec_name(bluetooth_active_codec_t codec);

/** @brief Return the stable string for one adapter state. */
const char *bluetooth_adapter_state_name(bluetooth_adapter_state_t state);

/** @brief Return the stable string for one disconnect reason. */
const char *bluetooth_disconnect_reason_name(
    bluetooth_disconnect_reason_t reason
);

/** @brief Initialize the Bluetooth audio store and adapter state machine. */
esp_err_t bluetooth_audio_init(void);

/** @brief Return true when the Bluetooth audio store is ready. */
bool bluetooth_audio_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t bluetooth_audio_set(const bluetooth_policy_t *policy);

/**
 * @brief Apply a policy fetched from the device endpoint.
 *
 * Maps the catalog codec name to the firmware enum and clamps every field.
 * Returns ESP_ERR_INVALID_ARG when the codec name is not in the catalog, so a
 * malformed response is rejected instead of silently defaulting the codec.
 */
esp_err_t bluetooth_audio_apply_remote(
    const bluetooth_audio_remote_t *remote
);

/** @brief Return the currently applied policy. */
bluetooth_policy_t bluetooth_audio_get(void);

/** @brief Apply a policy that is already version-compatible and bounded. */
esp_err_t bluetooth_audio_apply(const bluetooth_policy_t *policy);

/**
 * @brief Enable or disable Bluetooth audio.
 *
 * Disabling the feature drives the adapter off and revokes speaker mode and
 * pairing, so a disabled policy never leaves an active speaker running.
 */
esp_err_t bluetooth_audio_set_enabled(bool enabled);

/**
 * @brief Enter advertising so the device can accept a new pairing.
 *
 * Advertising is refused unless Bluetooth audio is enabled and the policy
 * permits pairing, so the device never exposes a pairing surface a guardian
 * did not authorise.
 */
esp_err_t bluetooth_audio_start_pairing(void);

/**
 * @brief Connect an active link and select the negotiated active codec.
 *
 * The connection is refused unless Bluetooth audio is enabled and the adapter
 * is idle or advertising. A successful connect transitions through the
 * connecting state into connected with a resolved active codec.
 */
esp_err_t bluetooth_audio_connect(bluetooth_active_codec_t codec);

/** @brief Disconnect the active link with a bounded reason. */
esp_err_t bluetooth_audio_disconnect(bluetooth_disconnect_reason_t reason);

/**
 * @brief Enable or disable speaker (A2DP sink) mode.
 *
 * Speaker mode can only be enabled while the adapter is connected and the
 * policy allows it; leaving the connected state always clears it.
 */
esp_err_t bluetooth_audio_set_speaker_mode(bool enabled);

/** @brief Report the current adapter state from the platform/RTOS layer. */
esp_err_t bluetooth_audio_report_adapter_state(
    bluetooth_adapter_state_t state
);

/** @brief Report the current output volume, clamped to the policy ceiling. */
esp_err_t bluetooth_audio_report_volume(uint8_t volume_percent);

/** @brief Return the current adapter state. */
bluetooth_adapter_state_t bluetooth_audio_adapter_state(void);

/** @brief Return the current active codec, or unknown when not connected. */
bluetooth_active_codec_t bluetooth_audio_active_codec(void);

/** @brief Return the reason the last link ended with. */
bluetooth_disconnect_reason_t bluetooth_audio_last_disconnect_reason(void);

/** @brief Return the current output volume in percent. */
uint8_t bluetooth_audio_volume_percent(void);

/** @brief Return true when speaker mode is currently active. */
bool bluetooth_audio_speaker_mode(void);

/** @brief Return the removable-module descriptor for bluetooth_audio. */
const module_descriptor_t *bluetooth_audio_module_descriptor(void);

#ifdef __cplusplus
}
#endif
