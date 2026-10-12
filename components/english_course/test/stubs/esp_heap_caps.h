#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

// Host stub for the ESP-IDF heap capability API. The firmware prefers PSRAM
// for the large lesson cache; on the host the capability flags are irrelevant
// and a plain malloc keeps the same allocation contract.
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_8BIT (1 << 2)

static inline void *heap_caps_malloc_prefer(size_t size, size_t num, ...) {
    (void)num;
    return malloc(size);
}

static inline void heap_caps_free(void *pointer) {
    free(pointer);
}
