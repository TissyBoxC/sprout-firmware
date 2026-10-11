#include "ui_text_store.h"

#include <string.h>

static bool ui_text_store_string_has_text(const char *value) {
    return value != NULL && value[0] != '\0';
}

static size_t ui_text_store_utf8_length(const char *value) {
    // The maximum length is declared in user-perceived characters. Counting
    // UTF-8 lead bytes is sufficient: continuation bytes are not lead bytes.
    size_t length = 0;
    for (const unsigned char *cursor = (const unsigned char *)value;
         cursor != NULL && *cursor != '\0';
         ++cursor) {
        if ((*cursor & 0xC0u) != 0x80u) {
            ++length;
        }
    }
    return length;
}

ui_text_store_result_t ui_text_store_init(
    ui_text_store_t *store,
    ui_text_entry_t *entries,
    size_t capacity
) {
    if (store == NULL || entries == NULL || capacity == 0) {
        return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
    }
    memset(store, 0, sizeof(*store));
    store->entries = entries;
    store->capacity = capacity;
    return UI_TEXT_STORE_OK;
}

bool ui_text_store_entry_is_valid(const ui_text_entry_t *entry) {
    if (entry == NULL) {
        return false;
    }
    if (!ui_text_store_string_has_text(entry->ui_text_key)) {
        return false;
    }
    if (entry->max_length == 0) {
        return false;
    }
    if (ui_text_store_utf8_length(entry->value) > entry->max_length) {
        return false;
    }
    if (entry->variable_count > UI_TEXT_MAX_VARIABLES) {
        return false;
    }
    for (uint8_t index = 0; index < entry->variable_count; ++index) {
        if (!ui_text_store_string_has_text(entry->variables[index])) {
            return false;
        }
    }
    return true;
}

int ui_text_store_compare_version(uint32_t left, uint32_t right) {
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

ui_text_store_result_t ui_text_store_apply_package(
    ui_text_store_t *store,
    const ui_text_package_t *package,
    const ui_text_entry_t *entries,
    size_t entry_count,
    bool *changed_out
) {
    if (changed_out != NULL) {
        *changed_out = false;
    }
    if (store == NULL || package == NULL) {
        return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
    }
    if (!ui_text_store_string_has_text(package->package_id) ||
        package->package_version == 0 || entry_count == 0) {
        return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
    }
    if (entry_count > store->capacity) {
        return UI_TEXT_STORE_ERR_FULL;
    }
    if (entries == NULL) {
        return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < entry_count; ++index) {
        if (!ui_text_store_entry_is_valid(&entries[index])) {
            return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
        }
    }
    if (store->active &&
        ui_text_store_compare_version(
            package->package_version,
            store->package.package_version
        ) < 0) {
        // An out of order delivery must never roll rendered text back.
        return UI_TEXT_STORE_OK;
    }

    store->package = *package;
    for (size_t index = 0; index < entry_count; ++index) {
        store->entries[index] = entries[index];
    }
    store->count = entry_count;
    store->active = true;
    if (changed_out != NULL) {
        *changed_out = true;
    }
    return UI_TEXT_STORE_OK;
}

ui_text_store_result_t ui_text_store_withdraw(
    ui_text_store_t *store,
    const char *package_id,
    bool *changed_out
) {
    if (changed_out != NULL) {
        *changed_out = false;
    }
    if (store == NULL || !ui_text_store_string_has_text(package_id)) {
        return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
    }
    if (!store->active ||
        strcmp(store->package.package_id, package_id) != 0) {
        return UI_TEXT_STORE_OK;
    }
    memset(&store->package, 0, sizeof(store->package));
    memset(store->entries, 0, store->capacity * sizeof(store->entries[0]));
    store->count = 0;
    store->active = false;
    if (changed_out != NULL) {
        *changed_out = true;
    }
    return UI_TEXT_STORE_OK;
}

ui_text_store_result_t ui_text_store_find(
    const ui_text_store_t *store,
    const char *ui_text_key,
    size_t *position_out
) {
    if (store == NULL || !ui_text_store_string_has_text(ui_text_key)) {
        return UI_TEXT_STORE_ERR_INVALID_ARGUMENT;
    }
    if (!store->active) {
        return UI_TEXT_STORE_ERR_NOT_FOUND;
    }
    for (size_t index = 0; index < store->count; ++index) {
        if (strcmp(store->entries[index].ui_text_key, ui_text_key) == 0) {
            if (position_out != NULL) {
                *position_out = index;
            }
            return UI_TEXT_STORE_OK;
        }
    }
    return UI_TEXT_STORE_ERR_NOT_FOUND;
}
