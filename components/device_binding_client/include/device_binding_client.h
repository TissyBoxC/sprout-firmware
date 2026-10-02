#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum length of a platform binding token including the terminator. */
#define DEVICE_BINDING_TOKEN_SIZE 129

/** @brief Maximum length of a platform device session token including the terminator. */
#define DEVICE_BINDING_SESSION_TOKEN_SIZE 129

/** @brief Callback delivered after the platform accepts a binding token. */
typedef void (*device_binding_completed_callback_t)(
    bool is_bound,
    void *context
);

/**
 * @brief Initialize the binding client state.
 *
 * Requires the configuration store. No network request is sent here.
 */
esp_err_t device_binding_client_init(void);

/**
 * @brief Register this device with the platform using a registration grant.
 *
 * The registration grant is created by manufacturing or support tooling. The
 * device sends its public key only; the private key never leaves NVS.
 */
esp_err_t device_binding_client_register(
    const char *registration_token,
    const char *hardware_model,
    const char *firmware_version
);

/**
 * @brief Authenticate with the platform and store a short-lived session token.
 *
 * A fresh challenge is requested for every call so a captured signature cannot
 * be replayed. Returns ESP_ERR_INVALID_STATE when the device is not registered.
 */
esp_err_t device_binding_client_authenticate(void);

/**
 * @brief Request a single-use binding token for the QR or BLE provisioning flow.
 *
 * The token is written to the caller buffer and is safe to place in a QR code.
 * The device session token is never exposed to the query string.
 */
esp_err_t device_binding_client_create_provisioning_token(
    char *output,
    size_t output_size
);

/** @brief Return true when a previous call bound the device to a guardian. */
bool device_binding_client_is_bound(void);

/** @brief Return true when the device has a persisted platform registration. */
bool device_binding_client_is_registered(void);

/**
 * @brief Register the binding completion observer.
 *
 * Passing NULL clears the observer. The callback runs on the caller task.
 */
esp_err_t device_binding_client_set_completion_callback(
    device_binding_completed_callback_t callback,
    void *context
);

/**
 * @brief Reload the persisted bound flag.
 *
 * Call after provisioning so a reboot does not repeat first-run setup.
 */
esp_err_t device_binding_client_refresh_state(void);

/**
 * @brief Ask the platform whether a guardian has completed binding.
 *
 * Uses the short-lived device session token. Returns ESP_ERR_INVALID_STATE
 * when the device has not authenticated yet.
 */
esp_err_t device_binding_client_check_binding(bool *is_bound_out);

/** @brief Return the removable-module descriptor for device_binding_client. */
const module_descriptor_t *device_binding_client_module_descriptor(void);

#ifdef __cplusplus
}
#endif
