#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"
#include "wearable_location.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One bounded position sample produced on the device.
 *
 * A wearable image supplies these values from its GNSS or network locator. The
 * sync worker never invents a position: when no locator is available it reports
 * no location at all instead of a placeholder coordinate.
 */
typedef struct {
    double latitude;
    double longitude;
    double accuracy_meters;
} wearable_sync_position_t;

/**
 * @brief On-device position provider.
 *
 * `read` returns ESP_OK and fills one bounded sample, ESP_ERR_NOT_FOUND when
 * there is no fix yet, or any other error to skip this cycle.
 */
typedef esp_err_t (*wearable_sync_position_fn)(
    void *context,
    wearable_sync_position_t *position_out
);

typedef struct {
    wearable_sync_position_fn read;
    void *context;
} wearable_sync_provider_t;

/**
 * @brief Register the on-device position provider.
 *
 * Passing NULL removes the provider. Without a provider the sync applies the
 * guardian policy but never reports a position.
 */
esp_err_t wearable_sync_register_provider(const wearable_sync_provider_t *provider);

/** @brief Apply the guardian policy and report location/SOS once. */
esp_err_t wearable_sync_now(void);

/** @brief Return the removable-module descriptor for wearable sync. */
const module_descriptor_t *wearable_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
