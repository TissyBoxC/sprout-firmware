#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Wi-Fi station lifecycle states exposed to other modules. */
typedef enum {
    NETWORK_MANAGER_STATE_IDLE = 0,
    NETWORK_MANAGER_STATE_CONNECTING,
    NETWORK_MANAGER_STATE_CONNECTED,
} network_manager_state_t;

/** @brief Callback invoked after every state transition. */
typedef void (*network_manager_state_callback_t)(
    network_manager_state_t state,
    void *context
);

/**
 * @brief Initialize the Wi-Fi station driver and register event handlers.
 *
 * The device starts disconnected. It only connects after credentials are
 * supplied, so a factory device never transmits on its own.
 */
esp_err_t network_manager_init(void);

/**
 * @brief Connect to an access point using the supplied credentials.
 *
 * Credentials are written to the configuration store before the connection is
 * attempted so a reboot can recover the same network. Returns
 * ESP_ERR_INVALID_ARG for an empty SSID or an over-length password.
 */
esp_err_t network_manager_connect(
    const char *ssid,
    const char *password
);

/**
 * @brief Store Wi-Fi credentials without changing the active connection.
 *
 * Provisioning uses this to persist the guardian-supplied network before the
 * provisioning service hands ownership of Wi-Fi back to network_manager.
 * Passwords are never returned or logged.
 */
esp_err_t network_manager_store_credentials(
    const char *ssid,
    const char *password
);

/** @brief Disconnect from the current access point and keep credentials. */
esp_err_t network_manager_disconnect(void);

/** @brief Return the current station state. */
network_manager_state_t network_manager_get_state(void);

/**
 * @brief Copy the SSID of the connected access point.
 *
 * Returns ESP_ERR_INVALID_STATE when the station is not connected.
 */
esp_err_t network_manager_copy_ssid(char *output, size_t output_size);

/**
 * @brief Register the single state observer.
 *
 * Passing NULL clears the observer. The callback runs on the Wi-Fi event task
 * and must not block or call back into the network manager.
 */
esp_err_t network_manager_set_state_callback(
    network_manager_state_callback_t callback,
    void *context
);

/**
 * @brief Reconnect using the stored credentials.
 *
 * Returns CONFIG_STORE_ERR_NOT_FOUND when the device has never been provisioned so
 * callers can start the provisioning flow instead.
 */
esp_err_t network_manager_reconnect_stored(void);

/** @brief Return the removable-module descriptor for network_manager. */
const module_descriptor_t *network_manager_module_descriptor(void);

#ifdef __cplusplus
}
#endif
