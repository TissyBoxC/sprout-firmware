#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*module_init_fn)(void);
typedef void (*module_shutdown_fn)(void);
typedef void (*module_failure_handler_t)(
    const char *module_name,
    esp_err_t error_code
);
typedef void (*module_recovery_handler_t)(const char *module_name);

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
 * @brief Retry one module that failed during registry initialization.
 *
 * Returns ESP_OK when the module is already initialized. A successful retry
 * clears the failure state and notifies the registered recovery handler once.
 * The caller must not invoke this concurrently with another registry retry.
 */
esp_err_t module_registry_initialize_module(const char *module_name);

/**
 * @brief Register the owner of failure notifications.
 *
 * The callback runs for every initialization failure, including failures
 * that occurred before the handler was registered. Passing NULL disables
 * notifications without changing initialization behavior.
 */
void module_registry_set_failure_handler(
    module_failure_handler_t handler
);

/**
 * @brief Register the owner of recovery notifications.
 *
 * The callback runs after a failed module succeeds on retry. Passing NULL
 * disables notifications without changing initialization behavior.
 */
void module_registry_set_recovery_handler(
    module_recovery_handler_t handler
);

/** @brief Return whether one module failed its most recent initialization. */
bool module_registry_has_failed(const char *module_name);

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
