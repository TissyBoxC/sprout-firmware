#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable errors returned by the device message module. */
typedef enum {
    DEVICE_MESSAGE_OK = 0,
    DEVICE_MESSAGE_ERR_NOT_INITIALIZED,
    DEVICE_MESSAGE_ERR_INVALID_ARG,
    DEVICE_MESSAGE_ERR_INVALID_PAYLOAD,
    DEVICE_MESSAGE_ERR_NO_DISPLAY,
    DEVICE_MESSAGE_ERR_BUSY,
} device_message_error_t;

/** @brief One bounded guardian message ready to render. */
typedef struct {
    char id[64];
    char title[128];
    char body[512];
    char severity[16];
    char category[32];
    uint32_t duration_seconds;
} device_message_t;

/** @brief Bounded counters for diagnostics and tests. */
typedef struct {
    uint32_t accepted;
    uint32_t rejected;
    uint32_t rendered;
    uint32_t display_unavailable;
    uint32_t feedback_failures;
    bool is_visible;
} device_message_snapshot_t;

/** @brief Initialize the device message presenter. */
esp_err_t device_message_init(void);

/** @brief Return true when the presenter can accept messages. */
bool device_message_is_ready(void);

/**
 * @brief Validate and present one guardian message.
 *
 * The payload is the authored copy from the platform runtime command. The
 * module never renders raw conversation content and rejects anything outside
 * the bounded contract.
 *
 * @param[in] message Parsed message. Title must be non-empty; duration is
 *                    clamped to the contract bounds.
 * @param[out] error_out Optional detailed error; may be NULL.
 *
 * @return ESP_OK when the message was accepted. ESP_OK is also returned when a
 *         display is absent but the message was recorded and acknowledged, so
 *         the platform does not retry a command the hardware cannot render.
 */
esp_err_t device_message_present(
    const device_message_t *message,
    device_message_error_t *error_out
);

/** @brief Copy the most recently presented message; returns false when none. */
bool device_message_last(device_message_t *message_out);

/** @brief Return the bounded presenter counters. */
device_message_snapshot_t device_message_get_snapshot(void);

/** @brief Return the stable string for one error. */
const char *device_message_error_name(device_message_error_t error);

/** @brief Return the removable-module descriptor for device_message. */
const module_descriptor_t *device_message_module_descriptor(void);

#ifdef __cplusplus
}
#endif
