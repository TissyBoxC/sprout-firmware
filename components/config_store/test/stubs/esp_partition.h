#pragma once

#include <stdint.h>

typedef enum {
    ESP_PARTITION_TYPE_DATA = 0,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_DATA_NVS = 0,
} esp_partition_subtype_t;

typedef struct {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    const char *label;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char *label
);
