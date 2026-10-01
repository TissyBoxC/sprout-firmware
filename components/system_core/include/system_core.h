#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize process-wide primitives required by feature modules.
 *
 * The function is idempotent so individual modules can initialize it during
 * tests without depending on application startup order.
 */
esp_err_t system_core_init(void);

/**
 * @brief Return the shared default event loop.
 *
 * The returned handle is valid after system_core_init succeeds.
 */
esp_event_loop_handle_t system_core_event_loop(void);

/** @brief Return the removable-module descriptor for system_core. */
const module_descriptor_t *system_core_module_descriptor(void);

#ifdef __cplusplus
}
#endif
