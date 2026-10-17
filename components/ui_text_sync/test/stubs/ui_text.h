#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define UI_TEXT_PACKAGE_ID_SIZE 40
#define UI_TEXT_LANGUAGE_SIZE 12
#define UI_TEXT_KEY_SIZE 64
#define UI_TEXT_VALUE_SIZE 256
#define UI_TEXT_VARIABLE_SIZE 16
#define UI_TEXT_MAX_VARIABLES 4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ui_text_key[UI_TEXT_KEY_SIZE];
    char value[UI_TEXT_VALUE_SIZE];
    uint16_t max_length;
    uint8_t variable_count;
    char variables[UI_TEXT_MAX_VARIABLES][UI_TEXT_VARIABLE_SIZE];
} ui_text_entry_t;

typedef struct {
    char package_id[UI_TEXT_PACKAGE_ID_SIZE];
    uint32_t package_version;
    char language[UI_TEXT_LANGUAGE_SIZE];
    uint32_t entry_count;
    int64_t published_at;
} ui_text_package_t;

bool ui_text_is_ready(void);
esp_err_t ui_text_active_package(ui_text_package_t *package_out);
esp_err_t ui_text_apply_package(
    const ui_text_package_t *package,
    const ui_text_entry_t *entries,
    size_t entry_count
);
esp_err_t ui_text_withdraw_package(const char *package_id);

#ifdef __cplusplus
}
#endif
