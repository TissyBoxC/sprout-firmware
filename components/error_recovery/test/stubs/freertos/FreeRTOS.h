#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

// The host test never reaches error_recovery_restart, so the critical-section
// macros only need to be balanced no-ops.
#define portMUX_INITIALIZER_UNLOCKED {0}
typedef struct {
    int lock;
} portMUX_TYPE;

#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
