#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"
#include "ota_manager_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANAGER_RELEASE_ID_SIZE 65
#define OTA_MANAGER_COMMAND_ID_SIZE 65
#define OTA_MANAGER_ERROR_CODE_SIZE 48

/** @brief Bounded OTA status snapshot exposed to diagnostics and the UI. */
typedef struct {
    ota_manager_stage_t stage;
    ota_manager_event_t last_event;
    uint32_t sequence;
    uint64_t received_bytes;
    uint64_t total_bytes;
    char release_id[OTA_MANAGER_RELEASE_ID_SIZE];
    char last_error[OTA_MANAGER_ERROR_CODE_SIZE];
    bool update_available;
    bool pending_reboot;
    bool ready;
} ota_manager_snapshot_t;

/** @brief Initialize OTA boot recovery and the update worker. */
esp_err_t ota_manager_init(void);

/** @brief Return true when the OTA worker is ready. */
bool ota_manager_is_ready(void);

/** @brief Return the current bounded OTA snapshot. */
ota_manager_snapshot_t ota_manager_get_snapshot(void);

/**
 * @brief Handle a `firmware_update` runtime command.
 *
 * The runtime reporter can route this command to the OTA worker without
 * coupling the reporter to partition or download details. The update is
 * idempotent by release ID and command ID.
 */
esp_err_t ota_manager_handle_firmware_update_command(
    const char *command_id,
    const char *release_id
);

/**
 * @brief Confirm the currently running OTA image after a health check.
 *
 * This must only be called from the image that was selected by a previous OTA
 * install. The call is idempotent and clears the pending release only after
 * ESP-IDF has accepted the image as valid.
 */
esp_err_t ota_manager_mark_running_image_healthy(void);

/** @brief Return the removable-module descriptor for ota_manager. */
const module_descriptor_t *ota_manager_module_descriptor(void);

#ifdef __cplusplus
}
#endif
