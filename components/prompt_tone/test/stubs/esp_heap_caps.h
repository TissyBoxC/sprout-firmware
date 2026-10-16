#pragma once

#include <stddef.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0

static inline void *heap_caps_calloc_prefer(
    size_t item_count,
    size_t item_size,
    size_t prefer_count,
    int prefer,
    int fallback
) {
    (void)prefer_count;
    (void)prefer;
    (void)fallback;
    return calloc(item_count, item_size);
}
