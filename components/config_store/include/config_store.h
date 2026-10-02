#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum length of a stored value excluding the null terminator. */
#define CONFIG_STORE_VALUE_SIZE 256

/** @brief Maximum length of a stored Wi-Fi SSID excluding the null terminator. */
#define CONFIG_STORE_SSID_SIZE 32

/** @brief Maximum length of a stored Wi-Fi password excluding the terminator. */
#define CONFIG_STORE_PASSWORD_SIZE 64

/**
 * @brief Result returned when a requested key has never been written.
 *
 * The value deliberately does not expose an NVS error code so removing
 * config_store does not force consumers to depend on the NVS component.
 */
#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND

/**
 * @brief Initialize the configuration namespace.
 *
 * Must run before any read or write. Production images must enable NVS
 * encryption for the partition that holds Wi-Fi credentials and device keys.
 */
esp_err_t config_store_init(void);

/** @brief Return true when the store is ready for reads and writes. */
bool config_store_is_ready(void);

/**
 * @brief Write one binary value and commit it.
 *
 * Binary values are used for manufacturing secrets such as provisioning SRP
 * material. The caller keeps ownership of the input buffer.
 */
esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
);

/**
 * @brief Read one binary value.
 *
 * Returns CONFIG_STORE_ERR_NOT_FOUND when the key is absent. On
 * ESP_ERR_NVS_INVALID_LENGTH, value_size_out contains the required size.
 */
esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
);

/**
 * @brief Write one string value and commit it.
 *
 * Passwords are never echoed, logged, or returned by this module.
 */
esp_err_t config_store_set_string(
    const char *key,
    const char *value
);

/**
 * @brief Read one string value.
 *
 * Returns CONFIG_STORE_ERR_NOT_FOUND when the key is absent and
 * ESP_ERR_NVS_INVALID_LENGTH when the output buffer is too small.
 */
esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
);

/** @brief Return true when one key exists in the store. */
esp_err_t config_store_has_key(const char *key, bool *has_key_out);

/** @brief Erase one key. Erasing an absent key succeeds. */
esp_err_t config_store_erase_key(const char *key);

/** @brief Return the removable-module descriptor for config_store. */
const module_descriptor_t *config_store_module_descriptor(void);

#ifdef __cplusplus
}
#endif
