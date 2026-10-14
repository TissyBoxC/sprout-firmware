#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define PARENT_POLICY_MAX_ALLOWED_CATEGORIES 16
#define PARENT_POLICY_CATEGORY_SIZE 32

typedef struct {
    int64_t policy_version;
    uint8_t max_volume_percent;
    char allowed_categories[PARENT_POLICY_MAX_ALLOWED_CATEGORIES]
                           [PARENT_POLICY_CATEGORY_SIZE];
    size_t allowed_category_count;
} parent_policy_snapshot_t;

typedef void (*parent_policy_change_callback_t)(void *context);

esp_err_t parent_policy_get_snapshot(parent_policy_snapshot_t *snapshot_out);
esp_err_t parent_policy_add_change_observer(
    parent_policy_change_callback_t callback,
    void *context
);
