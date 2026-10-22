#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "button_input.h"
#include "esp_err.h"
#include "module_registry.h"

#include "voice_intercom_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest guardian display name accepted from one call command. */
#define VOICE_INTERCOM_CALLER_NAME_SIZE 32

/** Maximum number of ringing prompts repeated while nobody answers. */
#define VOICE_INTERCOM_MAX_RING_PROMPTS 8

/** Bounded counters exposed for diagnostics and the parent application. */
typedef struct {
    voice_intercom_phase_t phase;
    voice_intercom_end_reason_t end_reason;
    char call_id[VOICE_INTERCOM_CALL_ID_SIZE];
    char caller_name[VOICE_INTERCOM_CALLER_NAME_SIZE];
    bool audio_allowed;
    uint32_t remaining_seconds;
    uint32_t calls_received;
    uint32_t calls_answered;
    uint32_t calls_declined;
    uint32_t calls_ended_by_guardian;
    uint32_t ring_timeouts;
    uint32_t duration_limits;
    uint32_t connect_failures;
    uint32_t commands_rejected;
} voice_intercom_snapshot_t;

/**
 * @brief Wire the module to the device command channel.
 *
 * Must be called before the first call command arrives. The handler is the
 * raw `device.command` payload that
 * `device_runtime_reporter_handle_mqtt_command` already acknowledged, so
 * starting a call is never tied to an acknowledgement retry. Only the
 * `voice_intercom_call` and `voice_intercom_call_control` command types are
 * accepted; everything else is counted and ignored.
 */
esp_err_t voice_intercom_init(void);

/** @brief Return true after a successful initialization. */
bool voice_intercom_is_ready(void);

/** @brief Return the module state before initialization. */
voice_intercom_phase_t voice_intercom_get_phase(void);

/** @brief Copy the bounded counters and the current call identity. */
esp_err_t voice_intercom_get_snapshot(voice_intercom_snapshot_t *snapshot_out);

/**
 * @brief Apply one physical button gesture to the active call.
 *
 * A short press accepts while ringing and hangs up while the call is active;
 * a long press declines while ringing and hangs up while the call is active.
 * Every other gesture and a device with no call is ignored, so the button
 * policy stays inside this module.
 */
esp_err_t voice_intercom_handle_button(button_input_gesture_t gesture);

/**
 * @brief Accept the ringing call from a physical accept event.
 *
 * This is the only entry point that can start audio. It opens the realtime
 * transport, acknowledges the call, starts the voice session, and switches to
 * ACTIVE only when the capture lease is actually held.
 */
esp_err_t voice_intercom_accept(void);

/**
 * @brief Decline the ringing call.
 *
 * Clears the ring indication immediately and reports the decline so the
 * guardian stops ringing. No transport is opened and no audio is captured.
 */
esp_err_t voice_intercom_decline(void);

/**
 * @brief Hang up the active call and release the audio path.
 *
 * Safe to call in every phase: without a call it is a no-op success so a
 * stale button press or a repeated command cannot fail the caller.
 */
esp_err_t voice_intercom_hangup(void);

/**
 * @brief Handle one raw `device.command` payload.
 *
 * A started call always begins in RINGING; a guardian control command can
 * only end a call, never accept it, so the accept step stays physically
 * mandatory.
 */
esp_err_t voice_intercom_handle_command(
    const char *payload,
    size_t payload_length
);

/**
 * @brief Advance the ring and call timers.
 *
 * The composition root calls this from its periodic tick (and the button and
 * command paths call it as well) so the ring timeout and the maximum call
 * duration end the call on the device even if no network event or button
 * press ever arrives. Returns the reason the call ended, or
 * VOICE_INTERCOM_END_NONE while it continues.
 */
voice_intercom_end_reason_t voice_intercom_poll(void);

/** @brief Return true only while the realtime audio session is running. */
bool voice_intercom_is_audio_active(void);

/** @brief Return the removable-module descriptor for voice_intercom. */
const module_descriptor_t *voice_intercom_module_descriptor(void);

#ifdef __cplusplus
}
#endif
