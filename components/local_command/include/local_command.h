#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of local command macros cached on the device. */
#define LOCAL_COMMAND_CAPACITY 24

/** @brief Size of a command id including the terminator. */
#define LOCAL_COMMAND_ID_SIZE 48

/** @brief Size of a trigger phrase including the terminator. */
#define LOCAL_COMMAND_PHRASE_SIZE 64

/** @brief Size of an action argument including the terminator. */
#define LOCAL_COMMAND_ARGUMENT_SIZE 96

/**
 * @brief Local action a command performs without the network.
 *
 * The numeric values are stable because they cross the persisted blob. They
 * stay in sync with the platform's local-command action names so an operator
 * can publish the same vocabulary the device already understands.
 */
typedef enum {
    LOCAL_COMMAND_ACTION_NONE = 0,
    LOCAL_COMMAND_ACTION_PLAY_PACKAGE = 1,
    LOCAL_COMMAND_ACTION_PAUSE = 2,
    LOCAL_COMMAND_ACTION_RESUME = 3,
    LOCAL_COMMAND_ACTION_STOP = 4,
    LOCAL_COMMAND_ACTION_SET_VOLUME = 5,
    LOCAL_COMMAND_ACTION_SLEEP = 6,
} local_command_action_t;

/** @brief One cached, offline-capable command macro. */
typedef struct {
    char command_id[LOCAL_COMMAND_ID_SIZE];
    char phrase[LOCAL_COMMAND_PHRASE_SIZE];
    local_command_action_t action;
    char argument[LOCAL_COMMAND_ARGUMENT_SIZE];
    uint32_t revision;
    bool enabled;
} local_command_t;

/** @brief Stable local command error codes for diagnostics and UI copy. */
typedef enum {
    LOCAL_COMMAND_OK = 0,
    LOCAL_COMMAND_ERR_NOT_INITIALIZED,
    LOCAL_COMMAND_ERR_INVALID_ARGUMENT,
    LOCAL_COMMAND_ERR_FULL,
    LOCAL_COMMAND_ERR_NOT_FOUND,
    LOCAL_COMMAND_ERR_STORAGE,
} local_command_error_t;

/** @brief Initialize the local command cache from the configuration store. */
esp_err_t local_command_init(void);

/** @brief Return true when the local command cache is ready. */
bool local_command_is_ready(void);

/**
 * @brief Replace the whole command set atomically.
 *
 * This is how the platform publishes a new command revision: the device
 * validates the batch first, so a malformed entry never partially replaces the
 * working set. Order is preserved.
 */
esp_err_t local_command_replace_all(
    const local_command_t *commands,
    size_t command_count
);

/** @brief Copy every cached command, in stored order. */
esp_err_t local_command_list(
    local_command_t *commands_out,
    size_t capacity,
    size_t *count_out
);

/**
 * @brief Resolve one trigger phrase to an action.
 *
 * The match is exact after the phrase is trimmed of surrounding whitespace and
 * folded to lower case, and disabled commands are never resolved. A matched,
 * enabled command that carries an argument does not imply the argument is
 * present.
 */
bool local_command_resolve(
    const char *phrase,
    local_command_t *command_out
);

/** @brief Return the current number of cached commands. */
size_t local_command_count(void);

/** @brief Return the stable string for one action. */
const char *local_command_action_name(local_command_action_t action);

/** @brief Parse a stable action name, or return false when unknown. */
bool local_command_action_from_name(
    const char *name,
    local_command_action_t *action_out
);

/** @brief Return the stable string for one local command error code. */
const char *local_command_error_name(local_command_error_t error);

/** @brief Return the removable-module descriptor for local_command. */
const module_descriptor_t *local_command_module_descriptor(void);

#ifdef __cplusplus
}
#endif
