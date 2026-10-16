#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM (1u << 10)
#define MALLOC_CAP_8BIT (1u << 2)

void *heap_caps_malloc_prefer(
    size_t size,
    size_t num,
    unsigned int caps1,
    unsigned int caps2
);
