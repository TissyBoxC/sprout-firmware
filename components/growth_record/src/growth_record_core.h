#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "growth_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure growth record store kept newest-change-first. */
typedef struct {
    growth_record_t records[GROWTH_RECORD_CAPACITY];
    size_t count;
} growth_record_core_t;

/** @brief Reset the store to empty. */
void growth_record_core_init(growth_record_core_t *core);

/** @brief Insert or replace one record, moving it to the head. */
growth_record_error_t growth_record_core_upsert(
    growth_record_core_t *core,
    const growth_record_t *record
);

/** @brief Remove one record; a missing id is success. */
growth_record_error_t growth_record_core_remove(
    growth_record_core_t *core,
    const char *record_id
);

/** @brief Remove every record for one child, reporting the removed count. */
growth_record_error_t growth_record_core_remove_child(
    growth_record_core_t *core,
    const char *child_id,
    size_t *removed_out
);

/** @brief Find one record index, or count when absent. */
size_t growth_record_core_find(
    const growth_record_core_t *core,
    const char *record_id
);

#ifdef __cplusplus
}
#endif
