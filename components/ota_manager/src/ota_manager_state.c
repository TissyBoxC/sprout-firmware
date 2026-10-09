#include "ota_manager_state.h"

static bool ota_manager_transition_allowed(
    ota_manager_stage_t current,
    ota_manager_stage_t next
) {
    if (current == next) {
        return true;
    }
    switch (current) {
        case OTA_MANAGER_STAGE_IDLE:
            return next == OTA_MANAGER_STAGE_CHECKING ||
                next == OTA_MANAGER_STAGE_FAILED ||
                next == OTA_MANAGER_STAGE_ROLLED_BACK;
        case OTA_MANAGER_STAGE_CHECKING:
            return next == OTA_MANAGER_STAGE_DOWNLOADING ||
                next == OTA_MANAGER_STAGE_IDLE ||
                next == OTA_MANAGER_STAGE_FAILED;
        case OTA_MANAGER_STAGE_DOWNLOADING:
            return next == OTA_MANAGER_STAGE_VALIDATING ||
                next == OTA_MANAGER_STAGE_FAILED ||
                next == OTA_MANAGER_STAGE_IDLE;
        case OTA_MANAGER_STAGE_VALIDATING:
            return next == OTA_MANAGER_STAGE_INSTALLING ||
                next == OTA_MANAGER_STAGE_FAILED;
        case OTA_MANAGER_STAGE_INSTALLING:
            return next == OTA_MANAGER_STAGE_PENDING_VERIFY ||
                next == OTA_MANAGER_STAGE_FAILED;
        case OTA_MANAGER_STAGE_PENDING_VERIFY:
            return next == OTA_MANAGER_STAGE_VALID ||
                next == OTA_MANAGER_STAGE_ROLLED_BACK ||
                next == OTA_MANAGER_STAGE_FAILED;
        case OTA_MANAGER_STAGE_VALID:
            return next == OTA_MANAGER_STAGE_IDLE ||
                next == OTA_MANAGER_STAGE_CHECKING;
        case OTA_MANAGER_STAGE_FAILED:
            return next == OTA_MANAGER_STAGE_IDLE ||
                next == OTA_MANAGER_STAGE_CHECKING ||
                next == OTA_MANAGER_STAGE_ROLLED_BACK;
        case OTA_MANAGER_STAGE_ROLLED_BACK:
            return next == OTA_MANAGER_STAGE_IDLE ||
                next == OTA_MANAGER_STAGE_CHECKING;
        default:
            return false;
    }
}

void ota_manager_state_init(ota_manager_state_t *state) {
    if (state == NULL) {
        return;
    }
    *state = (ota_manager_state_t) {
        .stage = OTA_MANAGER_STAGE_IDLE,
        .last_event = OTA_MANAGER_EVENT_NONE,
        .sequence = 0,
        .received_bytes = 0,
        .total_bytes = 0,
        .rollback_attempts = 0,
        .event_pending = false,
    };
}

bool ota_manager_state_transition(
    ota_manager_state_t *state,
    ota_manager_stage_t next_stage,
    ota_manager_event_t event
) {
    if (state == NULL ||
        !ota_manager_transition_allowed(state->stage, next_stage)) {
        return false;
    }
    if (next_stage == OTA_MANAGER_STAGE_FAILED ||
        next_stage == OTA_MANAGER_STAGE_ROLLED_BACK) {
        ++state->rollback_attempts;
    }
    state->stage = next_stage;
    if (event != OTA_MANAGER_EVENT_NONE) {
        state->last_event = event;
        ++state->sequence;
        state->event_pending = true;
    }
    return true;
}

void ota_manager_state_set_progress(
    ota_manager_state_t *state,
    uint64_t received_bytes,
    uint64_t total_bytes
) {
    if (state == NULL) {
        return;
    }
    state->received_bytes = received_bytes;
    state->total_bytes = total_bytes;
    state->last_event = OTA_MANAGER_EVENT_PROGRESS;
    ++state->sequence;
    state->event_pending = true;
}

ota_manager_event_t ota_manager_state_consume_event(
    ota_manager_state_t *state
) {
    if (state == NULL || !state->event_pending) {
        return OTA_MANAGER_EVENT_NONE;
    }
    state->event_pending = false;
    return state->last_event;
}

bool ota_manager_state_can_install(const ota_manager_state_t *state) {
    return state != NULL &&
        state->stage == OTA_MANAGER_STAGE_VALIDATING &&
        state->last_event == OTA_MANAGER_EVENT_VALIDATED;
}

const char *ota_manager_stage_name(ota_manager_stage_t stage) {
    switch (stage) {
        case OTA_MANAGER_STAGE_IDLE:
            return "idle";
        case OTA_MANAGER_STAGE_CHECKING:
            return "checking";
        case OTA_MANAGER_STAGE_DOWNLOADING:
            return "downloading";
        case OTA_MANAGER_STAGE_VALIDATING:
            return "validating";
        case OTA_MANAGER_STAGE_INSTALLING:
            return "installing";
        case OTA_MANAGER_STAGE_PENDING_VERIFY:
            return "pending_verify";
        case OTA_MANAGER_STAGE_VALID:
            return "valid";
        case OTA_MANAGER_STAGE_FAILED:
            return "failed";
        case OTA_MANAGER_STAGE_ROLLED_BACK:
            return "rolled_back";
        default:
            return "unknown";
    }
}

const char *ota_manager_event_name(ota_manager_event_t event) {
    switch (event) {
        case OTA_MANAGER_EVENT_NONE:
            return "none";
        case OTA_MANAGER_EVENT_STARTED:
            return "started";
        case OTA_MANAGER_EVENT_PROGRESS:
            return "progress";
        case OTA_MANAGER_EVENT_VALIDATED:
            return "validated";
        case OTA_MANAGER_EVENT_INSTALLED:
            return "installed";
        case OTA_MANAGER_EVENT_FAILED:
            return "failed";
        case OTA_MANAGER_EVENT_ROLLED_BACK:
            return "rolled_back";
        default:
            return "unknown";
    }
}
