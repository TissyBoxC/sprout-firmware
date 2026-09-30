#include "ui_text.h"

#include <stddef.h>

esp_err_t ui_text_resolve(const char *ui_text_key, const char **value) {
    if (ui_text_key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_ERR_NOT_FOUND;
}
