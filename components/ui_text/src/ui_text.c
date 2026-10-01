#include "ui_text.h"

#include <stddef.h>

esp_err_t ui_text_resolve(const char *ui_text_key, const char **value) {
    if (ui_text_key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t ui_text_initialize(void) {
    return ESP_OK;
}

const module_descriptor_t *ui_text_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "ui_text",
        .version = "1.0.0",
        .initialize = ui_text_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
