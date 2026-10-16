#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0

/* Implemented by the host stub so allocation failure can be injected. */
void *heap_caps_malloc_prefer(
    size_t size,
    size_t prefer_count,
    int prefer,
    int fallback
);
