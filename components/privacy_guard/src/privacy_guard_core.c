#include "privacy_guard_core.h"

#include <string.h>

static bool privacy_guard_core_class_is_valid(
    privacy_guard_data_class_t data_class
) {
    return data_class >= PRIVACY_GUARD_DATA_AUDIO_UPLOAD &&
        data_class < PRIVACY_GUARD_DATA_COUNT;
}

void privacy_guard_core_init(privacy_guard_core_state_t *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->state_version = PRIVACY_GUARD_STATE_VERSION;
}

bool privacy_guard_core_state_is_compatible(
    const privacy_guard_core_state_t *state
) {
    return state != NULL &&
        state->state_version == PRIVACY_GUARD_STATE_VERSION;
}

bool privacy_guard_core_is_allowed(
    const privacy_guard_core_state_t *state,
    privacy_guard_data_class_t data_class
) {
    if (state == NULL || !privacy_guard_core_class_is_valid(data_class)) {
        return false;
    }
    return state->consent[(size_t)data_class];
}
