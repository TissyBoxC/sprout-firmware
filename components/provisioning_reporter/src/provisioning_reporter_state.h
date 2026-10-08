#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "provisioning_reporter.h"

#define PROVISIONING_REPORTER_STATE_MAGIC 0x53505231u
#define PROVISIONING_REPORTER_STATE_VERSION 1u
#define PROVISIONING_REPORTER_MAX_STATE_SIZE 4096u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t event_count;
    uint32_t next_sequence;
    uint32_t dropped;
    uint8_t wifi_configured;
    uint8_t reserved[3];
    int64_t last_provisioned_epoch;
    char last_detail_code[PROVISIONING_REPORTER_DETAIL_CODE_SIZE];
    provisioning_reporter_event_t
        events[PROVISIONING_REPORTER_EVENT_CAPACITY];
} provisioning_reporter_state_t;

bool provisioning_reporter_text_is_terminated(
    const char *value,
    size_t buffer_size
);
bool provisioning_reporter_event_type_is_valid(
    provisioning_event_type_t type
);
bool provisioning_reporter_event_id_is_valid(const char *event_id);
bool provisioning_reporter_detail_code_is_valid(const char *detail_code);
bool provisioning_reporter_state_is_valid(
    const provisioning_reporter_state_t *state
);
bool provisioning_reporter_acknowledge_state(
    provisioning_reporter_state_t *state,
    uint32_t through_sequence
);

/** Increment the bounded drop counter without wrapping past the contract. */
uint32_t provisioning_reporter_next_dropped_count(uint32_t dropped);

/** Clamp a persisted drop counter before publishing it to the platform. */
uint32_t provisioning_reporter_clamp_dropped_count(uint32_t dropped);
