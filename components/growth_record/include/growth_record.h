#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of growth records cached on the device. */
#define GROWTH_RECORD_CAPACITY 24

/** @brief Size of a growth record id including the terminator. */
#define GROWTH_RECORD_ID_SIZE 40

/** @brief Size of a child id including the terminator. */
#define GROWTH_RECORD_CHILD_ID_SIZE 40

/** @brief Size of a growth record title including the terminator. */
#define GROWTH_RECORD_TITLE_SIZE 96

/** @brief Size of a growth record note including the terminator. */
#define GROWTH_RECORD_NOTE_SIZE 256

/**
 * @brief Growth record categories.
 *
 * The numeric values are stable because they cross the persisted blob; keep
 * them in sync with the platform's `milestone/encouragement/habit/emotion`.
 */
typedef enum {
    GROWTH_CATEGORY_MILESTONE = 0,
    GROWTH_CATEGORY_ENCOURAGEMENT = 1,
    GROWTH_CATEGORY_HABIT = 2,
    GROWTH_CATEGORY_EMOTION = 3,
} growth_category_t;

/** @brief One cached growth record, newest-first in the store. */
typedef struct {
    char record_id[GROWTH_RECORD_ID_SIZE];
    char child_id[GROWTH_RECORD_CHILD_ID_SIZE];
    growth_category_t category;
    char title[GROWTH_RECORD_TITLE_SIZE];
    char note[GROWTH_RECORD_NOTE_SIZE];
    int64_t recorded_at_epoch_seconds;
} growth_record_t;

/** @brief Stable growth-record error codes for diagnostics and UI copy. */
typedef enum {
    GROWTH_RECORD_OK = 0,
    GROWTH_RECORD_ERR_NOT_INITIALIZED,
    GROWTH_RECORD_ERR_INVALID_ARGUMENT,
    GROWTH_RECORD_ERR_FULL,
    GROWTH_RECORD_ERR_NOT_FOUND,
    GROWTH_RECORD_ERR_STORAGE,
} growth_record_error_t;

/** @brief Initialize the growth record cache from the configuration store. */
esp_err_t growth_record_init(void);

/** @brief Return true when the growth record cache is ready. */
bool growth_record_is_ready(void);

/** @brief Insert or replace one record, newest change first. */
esp_err_t growth_record_upsert(const growth_record_t *record);

/** @brief Remove one record. A missing record is success. */
esp_err_t growth_record_remove(const char *record_id);

/** @brief Remove every record for one child. Returns the removed count. */
esp_err_t growth_record_remove_child(const char *child_id, size_t *removed_out);

/** @brief Copy the cached records newest-first. */
esp_err_t growth_record_list(
    growth_record_t *records_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return one cached record by id. */
bool growth_record_get(const char *record_id, growth_record_t *record_out);

/** @brief Return the current number of cached records. */
size_t growth_record_count(void);

/** @brief Return the stable string for one growth category. */
const char *growth_record_category_name(growth_category_t category);

/** @brief Parse a stable category string, or return false when unknown. */
bool growth_record_category_from_name(
    const char *name,
    growth_category_t *category_out
);

/** @brief Return the stable string for one growth-record error code. */
const char *growth_record_error_name(growth_record_error_t error);

/** @brief Return the removable-module descriptor for growth_record. */
const module_descriptor_t *growth_record_module_descriptor(void);

#ifdef __cplusplus
}
#endif
