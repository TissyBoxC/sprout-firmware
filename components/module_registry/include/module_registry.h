#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*module_init_fn)(void);
typedef void (*module_shutdown_fn)(void);

/** @brief Metadata and lifecycle callbacks for one removable feature. */
typedef struct {
    const char *module_name;
    const char *version;
    module_init_fn initialize;
    module_shutdown_fn shutdown;
} module_descriptor_t;

/**
 * @brief Register one feature module.
 *
 * Registration is rejected after initialization starts so the composition
 * graph cannot change while the device is running.
 */
esp_err_t module_registry_add(const module_descriptor_t *descriptor);

/**
 * @brief Initialize every registered module in registration order.
 *
 * A failed optional module is recorded and skipped so unrelated modules can
 * still start. The function returns the first initialization error.
 */
esp_err_t module_registry_initialize_all(void);

/**
 * @brief Shut down initialized modules in reverse order.
 */
void module_registry_shutdown_all(void);

/**
 * @brief Return whether one module initialized successfully.
 */
bool module_registry_is_initialized(const char *module_name);

/**
 * @brief Return the number of successfully initialized modules.
 */
size_t module_registry_initialized_count(void);

#ifdef __cplusplus
}
#endif
