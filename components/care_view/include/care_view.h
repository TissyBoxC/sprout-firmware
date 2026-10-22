#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "button_input.h"
#include "esp_err.h"
#include "module_registry.h"

#include "care_view_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bounded counters exposed for diagnostics. */
typedef struct {
    care_view_phase_t phase;
    char session_id[CARE_VIEW_SESSION_ID_MAX];
    bool capture_allowed;
    uint32_t remaining_seconds;
    uint32_t frames_sent;
    uint32_t frames_dropped;
    uint32_t sessions_started;
    uint32_t sessions_refused;
    uint32_t child_closed;
    uint32_t expired;
    uint32_t camera_failures;
    uint32_t commands_rejected;
} care_view_status_t;

/**
 * @brief Wire the module to the device command channel.
 *
 * Must be called before the first care-view command arrives. The handler is the
 * raw `device.command` payload that arrives on the mutual-TLS command topic.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE when called twice, and
 *         ESP_ERR_INVALID_ARG when the channel cannot be subscribed.
 */
esp_err_t care_view_bind_command_channel(void);

/**
 * @brief Act on one command payload.
 *
 * Understands `care_view_start` and `care_view_stop` and ignores everything
 * else, so the module can share the command stream with other features. A
 * malformed, oversized, or unknown command is rejected without changing any
 * state: a bad frame must never be able to open the camera.
 *
 * @param payload NUL-terminated JSON payload, never null.
 * @param length  Payload length in bytes, excluding the terminator.
 * @return ESP_OK when the command was handled or deliberately ignored,
 *         ESP_ERR_INVALID_ARG when it was rejected.
 */
esp_err_t care_view_handle_command(const char *payload, size_t length);

/**
 * @brief Advance the deadline and drive capture.
 *
 * Called from a periodic timer. Returns immediately when idle, so it is cheap
 * to call every second regardless of state.
 */
void care_view_poll(void);

/** @brief Return the current status snapshot. */
care_view_status_t care_view_status(void);

/**
 * @brief Handle a button gesture while a view is running.
 *
 * Any press while a view is active stops it: the child must always be able to
 * end being watched with one physical action. Returns false so the caller keeps
 * its normal gesture handling.
 */
bool care_view_handle_button(button_input_gesture_t gesture);

/** @brief Stop any running view and release the camera. */
void care_view_shutdown(void);

/** @brief Module descriptor for the registry. */
const module_descriptor_t *care_view_module_descriptor(void);

#ifdef __cplusplus
}
#endif
