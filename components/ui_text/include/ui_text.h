#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve one user-visible text key.
 *
 * Resolution order is remote override, last known good package, then built-in
 * text. The returned pointer remains owned by the module and is valid until the
 * active package changes.
 *
 * @param[in] ui_text_key Stable key shared with the platform contract.
 * @param[out] value Resolved text. Set to the built-in value when no override
 *                   is active.
 *
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG when input or output is NULL;
 *         ESP_ERR_NOT_FOUND when the key is unknown.
 */
esp_err_t ui_text_resolve(const char *ui_text_key, const char **value);

#ifdef __cplusplus
}
#endif
