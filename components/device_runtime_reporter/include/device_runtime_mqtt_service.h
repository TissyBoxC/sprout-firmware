#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the device command MQTT channel when credentials are present.
 *
 * The broker URL, broker CA, device client certificate, and device client key
 * are read from the configuration store using the keys below. Nothing is
 * hardcoded; a device without provisioning stays on the authenticated HTTP
 * poll and merely logs that the MQTT channel is not configured.
 *
 * The service registers a Wi-Fi state observer and (re)connects the MQTT
 * client whenever the station reaches `connected`. Calling the function twice
 * is idempotent.
 */
esp_err_t device_runtime_mqtt_service_start(void);

/** @brief Stop the MQTT channel and clear its Wi-Fi observer. */
esp_err_t device_runtime_mqtt_service_stop(void);

/** @brief Return true while the MQTT command channel is connected. */
bool device_runtime_mqtt_service_is_connected(void);

#ifdef __cplusplus
}
#endif
