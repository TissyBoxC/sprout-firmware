#pragma once

// Minimal single-threaded host stand-in for the monotonic timestamp the
// conversation context stamps onto each turn. The exact value is irrelevant to
// the gating test, so it returns a fixed microsecond reading.

#include <stdint.h>

static inline int64_t esp_timer_get_time(void) {
    return 1000000;
}
