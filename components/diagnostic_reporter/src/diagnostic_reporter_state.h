#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diagnostic_reporter.h"

#define DIAGNOSTIC_REPORTER_STATE_MAGIC 0x53444731u
#define DIAGNOSTIC_REPORTER_STATE_VERSION 5u
#define DIAGNOSTIC_REPORTER_STATE_VERSION_V4 4u
#define DIAGNOSTIC_REPORTER_STATE_VERSION_V3 3u
#define DIAGNOSTIC_REPORTER_MAX_STATE_SIZE 8192u

typedef struct {
    uint32_t sequence;
    uint32_t failure_count;
    uint32_t source_transition_count;
    uint8_t pending;
    char module_name[DIAGNOSTIC_REPORTER_MODULE_NAME_SIZE];
    char error_code[DIAGNOSTIC_REPORTER_ERROR_CODE_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_failure_state_t;

typedef struct {
    uint32_t sequence;
    uint32_t source_transition_count;
    char event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE];
    char module_name[DIAGNOSTIC_REPORTER_MODULE_NAME_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_recovery_state_t;

typedef struct {
    uint32_t sequence;
    uint32_t duration_ms;
    char event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE];
    char event_type[DIAGNOSTIC_REPORTER_EVENT_TYPE_SIZE];
    char detail_code[DIAGNOSTIC_REPORTER_DETAIL_CODE_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_interaction_state_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t boot_event_count;
    uint32_t next_sequence;
    uint32_t boot_count;
    uint32_t dropped_boot_events;
    uint32_t source_boot_count;
    uint32_t last_failure_transition_count;
    uint32_t last_recovery_transition_count;
    diagnostic_reporter_failure_state_t failure;
    uint16_t recovery_event_count;
    diagnostic_reporter_recovery_state_t
        recovery_events[DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY];
    diagnostic_reporter_boot_event_t
        boot_events[DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY];
} diagnostic_reporter_state_v3_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t boot_event_count;
    uint32_t next_sequence;
    uint32_t boot_count;
    uint32_t dropped_boot_events;
    uint32_t source_boot_count;
    uint32_t last_failure_transition_count;
    uint32_t last_recovery_transition_count;
    uint16_t interaction_event_count;
    diagnostic_reporter_failure_state_t failure;
    uint16_t recovery_event_count;
    diagnostic_reporter_recovery_state_t
        recovery_events[DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY];
    diagnostic_reporter_interaction_state_t
        interaction_events[DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY];
    diagnostic_reporter_boot_event_t
        boot_events[DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY];
} diagnostic_reporter_state_v4_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t boot_event_count;
    uint32_t next_sequence;
    uint32_t boot_count;
    uint32_t dropped_boot_events;
    uint32_t source_boot_count;
    uint32_t last_failure_transition_count;
    uint32_t last_recovery_transition_count;
    uint16_t interaction_event_count;
    diagnostic_reporter_failure_state_t failure;
    uint16_t recovery_event_count;
    diagnostic_reporter_recovery_state_t
        recovery_events[DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY];
    diagnostic_reporter_interaction_state_t
        interaction_events[DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY];
    diagnostic_reporter_boot_event_t
        boot_events[DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY];
} diagnostic_reporter_state_t;

bool diagnostic_reporter_text_is_terminated(
    const char *value,
    size_t buffer_size
);
bool diagnostic_reporter_v3_is_valid(
    const diagnostic_reporter_state_v3_t *state
);
bool diagnostic_reporter_v4_is_valid(
    const diagnostic_reporter_state_v4_t *state
);
bool diagnostic_reporter_v5_is_valid(
    const diagnostic_reporter_state_t *state
);
bool diagnostic_reporter_acknowledge_state(
    diagnostic_reporter_state_t *state,
    uint32_t through_sequence
);
void diagnostic_reporter_migrate_v3(
    diagnostic_reporter_state_t *destination,
    const diagnostic_reporter_state_v3_t *source
);
void diagnostic_reporter_migrate_v4(
    diagnostic_reporter_state_t *destination,
    const diagnostic_reporter_state_v4_t *source
);
