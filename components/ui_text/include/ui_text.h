#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum package identifier length including the terminator. */
#define UI_TEXT_PACKAGE_ID_SIZE 40

/** @brief Maximum language tag length including the terminator. */
#define UI_TEXT_LANGUAGE_SIZE 12

/** @brief Maximum text key length including the terminator. */
#define UI_TEXT_KEY_SIZE 64

/** @brief Maximum resolved value length including the terminator. */
#define UI_TEXT_VALUE_SIZE 256

/** @brief Maximum variable name length including the terminator. */
#define UI_TEXT_VARIABLE_SIZE 16

/** @brief Maximum variables declared by one entry. */
#define UI_TEXT_MAX_VARIABLES 4

/** @brief One text entry carried in a remote package. */
typedef struct {
    char ui_text_key[UI_TEXT_KEY_SIZE];
    char value[UI_TEXT_VALUE_SIZE];
    uint16_t max_length;
    uint8_t variable_count;
    char variables[UI_TEXT_MAX_VARIABLES][UI_TEXT_VARIABLE_SIZE];
} ui_text_entry_t;

/** @brief The active package applied to the device. */
typedef struct {
    char package_id[UI_TEXT_PACKAGE_ID_SIZE];
    uint32_t package_version;
    char language[UI_TEXT_LANGUAGE_SIZE];
    uint32_t entry_count;
    int64_t published_at;
} ui_text_package_t;

/** @brief Stable module error codes for diagnostics and UI copy. */
typedef enum {
    UI_TEXT_OK = 0,
    UI_TEXT_ERR_NOT_INITIALIZED,
    UI_TEXT_ERR_INVALID_ARGUMENT,
    UI_TEXT_ERR_FULL,
    UI_TEXT_ERR_NOT_FOUND,
    UI_TEXT_ERR_STORAGE,
    UI_TEXT_ERR_CORRUPT_INDEX,
} ui_text_error_t;

/** @brief Initialize the active package from the configuration store. */
esp_err_t ui_text_init(void);

/** @brief Return true when the store has loaded successfully. */
bool ui_text_is_ready(void);

/** @brief Return true when a remote package is active. */
bool ui_text_has_active_package(void);

/** @brief Copy the active package metadata. */
esp_err_t ui_text_active_package(ui_text_package_t *package_out);

/**
 * @brief Replace the active package with one remotely published revision.
 *
 * A package with a version older than the active one is ignored so an out of
 * order delivery cannot roll text back. An equal version refreshes the entries.
 * Entries are validated against their declared maximum length; the first
 * invalid entry rejects the whole package so a partial package never renders.
 */
esp_err_t ui_text_apply_package(
    const ui_text_package_t *package,
    const ui_text_entry_t *entries,
    size_t entry_count
);

/** @brief Remove the active package and revert to built-in text. */
esp_err_t ui_text_withdraw_package(const char *package_id);

/**
 * @brief Resolve one user-visible text key.
 *
 * Resolution order is the active remote package, then built-in text supplied by
 * the caller. The returned pointer remains owned by the module and is valid
 * until the active package changes.
 *
 * @param[in] ui_text_key Stable key shared with the platform contract.
 * @param[in] fallback Built-in text used when no remote entry matches. May be
 *                     NULL, in which case ESP_ERR_NOT_FOUND is returned when
 *                     there is no remote override.
 * @param[out] value Resolved text.
 *
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG when a required pointer is
 *         NULL; ESP_ERR_NOT_FOUND when the key has no remote or built-in value.
 */
esp_err_t ui_text_resolve(
    const char *ui_text_key,
    const char *fallback,
    const char **value
);

/** @brief Return the stable string for one module error code. */
const char *ui_text_error_name(ui_text_error_t error);

/** @brief Return the removable-module descriptor for remote UI text. */
const module_descriptor_t *ui_text_module_descriptor(void);

#ifdef __cplusplus
}
#endif
