#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "privacy_guard.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of data classes tracked by the guard. */
#define PRIVACY_GUARD_CORE_CLASS_COUNT 4u

/** @brief Persisted layout version; a mismatch resets to deny-all. */
#define PRIVACY_GUARD_STATE_VERSION 1u

/** @brief Persisted state owned by the pure guard core. */
typedef struct {
    uint32_t state_version;
    bool consent[PRIVACY_GUARD_CORE_CLASS_COUNT];
    uint32_t consent_revision;
} privacy_guard_core_state_t;

/** @brief Reset a core state to deny-all with the current layout version. */
void privacy_guard_core_init(privacy_guard_core_state_t *state);

/** @brief Return true when a restored state matches the current layout. */
bool privacy_guard_core_state_is_compatible(
    const privacy_guard_core_state_t *state
);

/** @brief Return whether one class is currently allowed. */
bool privacy_guard_core_is_allowed(
    const privacy_guard_core_state_t *state,
    privacy_guard_data_class_t data_class
);

#ifdef __cplusplus
}
#endif
