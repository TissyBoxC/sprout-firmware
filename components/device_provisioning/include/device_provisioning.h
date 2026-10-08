#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Provisioning events observed by the composition root. */
typedef enum {
    DEVICE_PROVISIONING_EVENT_STARTED = 0,
    DEVICE_PROVISIONING_EVENT_WIFI_CONFIGURED,
    DEVICE_PROVISIONING_EVENT_WIFI_FAILED,
} device_provisioning_event_t;

/** @brief Callback invoked after a provisioning lifecycle transition. */
typedef void (*device_provisioning_event_callback_t)(
    device_provisioning_event_t event,
    const char *detail_code,
    void *context
);

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

/**
 * @brief Register the provisioning lifecycle observer.
 *
 * Passing NULL clears the observer. The callback runs on the provisioning
 * manager's event task and must not block.
 */
esp_err_t device_provisioning_set_event_callback(
    device_provisioning_event_callback_t callback,
    void *context
);

/** @brief Return the removable-module descriptor for device_provisioning. */
const module_descriptor_t *device_provisioning_module_descriptor(void);

#ifdef __cplusplus
}
#endif
