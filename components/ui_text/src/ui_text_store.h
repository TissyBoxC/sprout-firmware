#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result codes returned by the pure store implementation. */
typedef enum {
    UI_TEXT_STORE_OK = 0,
    UI_TEXT_STORE_ERR_INVALID_ARGUMENT,
    UI_TEXT_STORE_ERR_FULL,
    UI_TEXT_STORE_ERR_NOT_FOUND,
} ui_text_store_result_t;

/** @brief Caller-owned container holding the active package and its entries. */
typedef struct {
    ui_text_package_t package;
    ui_text_entry_t *entries;
    size_t capacity;
    size_t count;
    bool active;
} ui_text_store_t;

/** @brief Bind a caller-owned entry array to the store. */
ui_text_store_result_t ui_text_store_init(
    ui_text_store_t *store,
    ui_text_entry_t *entries,
    size_t capacity
);

/** @brief Validate one entry name, value, and declared length. */
bool ui_text_store_entry_is_valid(const ui_text_entry_t *entry);

/** @brief Compare package versions. */
int ui_text_store_compare_version(uint32_t left, uint32_t right);

/**
 * @brief Replace the active package.
 *
 * An older version is ignored, an equal version refreshes the entries, and a
 * newer version replaces the package. The package is rejected as a whole when
 * any entry is invalid or the entry count exceeds the store capacity.
 */
ui_text_store_result_t ui_text_store_apply_package(
    ui_text_store_t *store,
    const ui_text_package_t *package,
    const ui_text_entry_t *entries,
    size_t entry_count,
    bool *changed_out
);

/** @brief Remove the active package when the id matches. */
ui_text_store_result_t ui_text_store_withdraw(
    ui_text_store_t *store,
    const char *package_id,
    bool *changed_out
);

/** @brief Find one entry by text key. */
ui_text_store_result_t ui_text_store_find(
    const ui_text_store_t *store,
    const char *ui_text_key,
    size_t *position_out
);

#ifdef __cplusplus
}
#endif
