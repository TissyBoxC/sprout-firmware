#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_MANAGER_STAGE_IDLE = 0,
    OTA_MANAGER_STAGE_CHECKING,
    OTA_MANAGER_STAGE_DOWNLOADING,
    OTA_MANAGER_STAGE_VALIDATING,
    OTA_MANAGER_STAGE_INSTALLING,
    OTA_MANAGER_STAGE_PENDING_VERIFY,
    OTA_MANAGER_STAGE_VALID,
    OTA_MANAGER_STAGE_FAILED,
    OTA_MANAGER_STAGE_ROLLED_BACK,
} ota_manager_stage_t;

typedef enum {
    OTA_MANAGER_EVENT_NONE = 0,
    OTA_MANAGER_EVENT_STARTED,
    OTA_MANAGER_EVENT_PROGRESS,
    OTA_MANAGER_EVENT_VALIDATED,
    OTA_MANAGER_EVENT_INSTALLED,
    OTA_MANAGER_EVENT_FAILED,
    OTA_MANAGER_EVENT_ROLLED_BACK,
} ota_manager_event_t;

typedef struct {
    ota_manager_stage_t stage;
    ota_manager_event_t last_event;
    uint32_t sequence;
    uint64_t received_bytes;
    uint64_t total_bytes;
    uint32_t rollback_attempts;
    bool event_pending;
} ota_manager_state_t;

void ota_manager_state_init(ota_manager_state_t *state);

bool ota_manager_state_transition(
    ota_manager_state_t *state,
    ota_manager_stage_t next_stage,
    ota_manager_event_t event
);

void ota_manager_state_set_progress(
    ota_manager_state_t *state,
    uint64_t received_bytes,
    uint64_t total_bytes
);

ota_manager_event_t ota_manager_state_consume_event(
    ota_manager_state_t *state
);

bool ota_manager_state_can_install(const ota_manager_state_t *state);

const char *ota_manager_stage_name(ota_manager_stage_t stage);
const char *ota_manager_event_name(ota_manager_event_t event);

#ifdef __cplusplus
}
#endif
