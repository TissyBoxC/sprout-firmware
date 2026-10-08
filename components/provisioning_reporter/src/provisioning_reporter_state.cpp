#include "provisioning_reporter_state.h"

#include <string.h>

namespace {

constexpr uint32_t kMaximumDurationMs = 24u * 60u * 60u * 1000u;

const char *const kEventNames[] = {
    "provisioning_started",
    "wifi_configured",
    "wifi_failed",
    "binding_completed",
    "binding_removed",
    "network_reconnected",
    "network_lost",
    "time_synced",
    "auth_revoked",
    "auth_restored",
    "binding_confirmed",
    "binding_pending",
};

constexpr size_t kEventNameCount =
    sizeof(kEventNames) / sizeof(kEventNames[0]);

bool event_is_valid(const provisioning_reporter_event_t *event) {
    return event->sequence != 0 &&
        event->duration_ms <= kMaximumDurationMs &&
        provisioning_reporter_text_is_terminated(
            event->event_id,
            sizeof(event->event_id)
        ) &&
        provisioning_reporter_text_is_terminated(
            event->event_type,
            sizeof(event->event_type)
        ) &&
        provisioning_reporter_text_is_terminated(
            event->detail_code,
            sizeof(event->detail_code)
        ) &&
        provisioning_reporter_text_is_terminated(
            event->firmware_version,
            sizeof(event->firmware_version)
        ) &&
        provisioning_reporter_event_id_is_valid(event->event_id) &&
        provisioning_reporter_detail_code_is_valid(event->detail_code);
}

}  // namespace

const char *provisioning_reporter_event_type_name(
    provisioning_event_type_t type
) {
    if (!provisioning_reporter_event_type_is_valid(type)) {
        return "";
    }
    return kEventNames[static_cast<size_t>(type)];
}

bool provisioning_reporter_text_is_terminated(
    const char *value,
    size_t buffer_size
) {
    if (value == nullptr || buffer_size == 0) {
        return false;
    }
    for (size_t index = 0; index < buffer_size; ++index) {
        if (value[index] == '\0') {
            return true;
        }
    }
    return false;
}

bool provisioning_reporter_event_type_is_valid(
    provisioning_event_type_t type
) {
    return static_cast<unsigned int>(type) < kEventNameCount;
}

bool provisioning_reporter_detail_code_is_valid(const char *detail_code) {
    if (!provisioning_reporter_text_is_terminated(
            detail_code,
            PROVISIONING_REPORTER_DETAIL_CODE_SIZE
        )) {
        return false;
    }
    const size_t length = strlen(detail_code);
    if (length < 1 || length > PROVISIONING_REPORTER_DETAIL_CODE_SIZE - 1) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value =
            static_cast<unsigned char>(detail_code[index]);
        const bool is_ascii_letter =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z');
        const bool is_ascii_digit = value >= '0' && value <= '9';
        const bool is_symbol =
            value == '_' || value == '.' || value == ':' || value == '-';
        if (!is_ascii_letter && !is_ascii_digit && !is_symbol) {
            return false;
        }
    }
    return true;
}

bool provisioning_reporter_event_id_is_valid(const char *event_id) {
    if (!provisioning_reporter_text_is_terminated(
            event_id,
            PROVISIONING_REPORTER_EVENT_ID_SIZE
        )) {
        return false;
    }
    const size_t length = strlen(event_id);
    if (length < 4 || length > 64 || event_id[0] < 'a' ||
        event_id[0] > 'z') {
        return false;
    }
    for (size_t index = 1; index < length; ++index) {
        const unsigned char value =
            static_cast<unsigned char>(event_id[index]);
        const bool is_ascii_letter = value >= 'a' && value <= 'z';
        const bool is_ascii_digit = value >= '0' && value <= '9';
        if (!is_ascii_letter && !is_ascii_digit && value != '_') {
            return false;
        }
    }
    return true;
}

bool provisioning_reporter_state_is_valid(
    const provisioning_reporter_state_t *state
) {
    if (state == nullptr ||
        state->magic != PROVISIONING_REPORTER_STATE_MAGIC ||
        state->version != PROVISIONING_REPORTER_STATE_VERSION ||
        state->event_count > PROVISIONING_REPORTER_EVENT_CAPACITY ||
        state->wifi_configured > 1 ||
        state->last_provisioned_epoch < 0 ||
        !provisioning_reporter_text_is_terminated(
            state->last_detail_code,
            sizeof(state->last_detail_code)
        )) {
        return false;
    }

    uint32_t previous_sequence = 0;
    for (size_t index = 0; index < state->event_count; ++index) {
        const provisioning_reporter_event_t *event = &state->events[index];
        if (!event_is_valid(event) || event->sequence <= previous_sequence) {
            return false;
        }
        bool type_matches = false;
        for (size_t type_index = 0; type_index < kEventNameCount; ++type_index) {
            if (strcmp(
                    event->event_type,
                    kEventNames[type_index]
                ) == 0) {
                type_matches = true;
                break;
            }
        }
        if (!type_matches) {
            return false;
        }
        previous_sequence = event->sequence;
    }
    return state->next_sequence != 0 &&
        (previous_sequence == 0 ||
         state->next_sequence > previous_sequence);
}

bool provisioning_reporter_acknowledge_state(
    provisioning_reporter_state_t *state,
    uint32_t through_sequence
) {
    if (state == nullptr || through_sequence == 0) {
        return state != nullptr;
    }

    size_t retained_count = 0;
    for (size_t index = 0; index < state->event_count; ++index) {
        const provisioning_reporter_event_t *event = &state->events[index];
        if (event->sequence > through_sequence) {
            state->events[retained_count++] = *event;
        }
    }
    state->event_count = static_cast<uint16_t>(retained_count);
    return true;
}

uint32_t provisioning_reporter_next_dropped_count(uint32_t dropped) {
    const uint32_t clamped = provisioning_reporter_clamp_dropped_count(dropped);
    if (clamped >= PROVISIONING_REPORTER_DROPPED_LIMIT) {
        return PROVISIONING_REPORTER_DROPPED_LIMIT;
    }
    return clamped + 1;
}

uint32_t provisioning_reporter_clamp_dropped_count(uint32_t dropped) {
    if (dropped >= PROVISIONING_REPORTER_DROPPED_LIMIT) {
        return PROVISIONING_REPORTER_DROPPED_LIMIT;
    }
    return dropped;
}
