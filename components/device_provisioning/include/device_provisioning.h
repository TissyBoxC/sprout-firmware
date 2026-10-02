#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start BLE provisioning when the device is not yet bound.
 *
 * The service uses Security 2 and a per-device proof of possession. A device
 * that is already bound only reconnects stored Wi-Fi credentials.
 */
esp_err_t device_provisioning_init(void);

/** @brief Return true while the BLE provisioning service is advertising. */
bool device_provisioning_is_active(void);

/**
 * @brief Copy the first-run setup URI for a display or local setup screen.
 *
 * The URI is available only while an unbound device is advertising. It carries
 * a temporary, per-device proof of possession and never a platform token. The
 * caller owns the output buffer and must erase it after the setup screen is
 * closed.
 */
esp_err_t device_provisioning_copy_setup_payload(
    char *output,
    size_t output_size
);

/** @brief Return the removable-module descriptor for device_provisioning. */
const module_descriptor_t *device_provisioning_module_descriptor(void);

#ifdef __cplusplus
}
#endif
