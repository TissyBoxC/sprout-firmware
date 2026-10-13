#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cellular_4g.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One live modem reading supplied by the cellular driver.
 *
 * A cellular-capable image supplies these values from its modem. Without a
 * provider the worker still applies the guardian policy but reports an absent
 * eSIM and zero signal rather than a fabricated reading.
 */
typedef struct {
    uint32_t used_mb;
    uint8_t signal_percent;
    bool registered;
    bool roaming;
    cellular_esim_state_t esim_state;
} cellular_sync_status_t;

typedef esp_err_t (*cellular_sync_status_fn)(
    void *context,
    cellular_sync_status_t *status_out
);

typedef struct {
    cellular_sync_status_fn read;
    void *context;
} cellular_sync_provider_t;

/** @brief Register the modem status provider; NULL removes it. */
esp_err_t cellular_sync_register_provider(const cellular_sync_provider_t *provider);

/** @brief Read the guardian policy, apply it and report the modem status once. */
esp_err_t cellular_sync_now(void);

/** @brief Return the removable-module descriptor for cellular sync. */
const module_descriptor_t *cellular_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
