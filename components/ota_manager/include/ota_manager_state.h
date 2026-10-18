#pragma once

#include <stdbool.h>
#include <stddef.h>
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

bool ota_manager_state_set_progress(
    ota_manager_state_t *state,
    uint64_t received_bytes,
    uint64_t total_bytes
);

/**
 * @brief Atomically enter validation after a completed download.
 *
 * The last event deliberately remains PROGRESS: it records the transport
 * state and must not be confused with a completed signature and partition
 * validation. Installation checks the validation booleans separately.
 */
bool ota_manager_state_begin_validation(
    ota_manager_state_t *state,
    uint64_t received_bytes,
    uint64_t total_bytes
);

/**
 * @brief Atomically gate and enter the installation stage.
 *
 * Returns false without changing state unless every signed-policy and
 * artifact validation has succeeded.
 */
bool ota_manager_state_begin_install(
    ota_manager_state_t *state,
    bool manifest_validated,
    bool signature_validated,
    bool device_policy_passed,
    bool artifact_download_validated
);

/**
 * @brief Atomically enter pending verification after boot selection.
 */
bool ota_manager_state_begin_pending_verify(
    ota_manager_state_t *state
);

ota_manager_event_t ota_manager_state_consume_event(
    ota_manager_state_t *state
);

bool ota_manager_state_can_install(const ota_manager_state_t *state);

/**
 * @brief Decide whether a pending image may be confirmed healthy.
 *
 * A successful platform status request and a complete persisted install
 * transaction are both required. A network failure or missing transaction
 * must leave the image pending so rollback remains possible.
 */
bool ota_manager_state_can_mark_healthy(
    const ota_manager_state_t *state,
    bool platform_status_ready,
    bool pending_transaction_trusted
);

/**
 * @brief Decide whether a validated manifest may enter OTA installation.
 *
 * Mandatory releases stay gated on all signed policy, device, and artifact
 * validation succeeding. They are never installed from a partial or degraded
 * validation path.
 */
bool ota_manager_state_can_install_release(
    const ota_manager_state_t *state,
    bool manifest_validated,
    bool signature_validated,
    bool device_policy_passed,
    bool artifact_download_validated
);

const char *ota_manager_stage_name(ota_manager_stage_t stage);
const char *ota_manager_event_name(ota_manager_event_t event);

#ifdef __cplusplus
}
#endif
