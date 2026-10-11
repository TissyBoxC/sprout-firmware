#include "motion_control.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "motion_core.h"

// One bounded binary blob persists the guardian motion policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define MOTION_STORE_KEY "motion_policy"

typedef struct {
    uint8_t actuator;
    motion_action_t action;
    int16_t angle_degrees;
    uint8_t speed_percent;
    bool used;
} motion_slot_t;

static motion_policy_t s_policy;
static motion_slot_t s_queue[MOTION_MAX_ACTUATORS];
static size_t s_queue_depth;
static motion_action_t s_action;
static motion_state_t s_state;
static bool s_ready;

static esp_err_t motion_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        MOTION_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void motion_restore(void) {
    motion_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        MOTION_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        motion_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = motion_default();
}

static void motion_drop_queue(void) {
    memset(s_queue, 0, sizeof(s_queue));
    s_queue_depth = 0u;
}

static motion_slot_t *motion_find_slot(uint8_t actuator) {
    for (size_t index = 0; index < MOTION_MAX_ACTUATORS; ++index) {
        if (s_queue[index].used && s_queue[index].actuator == actuator) {
            return &s_queue[index];
        }
    }
    return NULL;
}

static motion_slot_t *motion_free_slot(void) {
    for (size_t index = 0; index < MOTION_MAX_ACTUATORS; ++index) {
        if (!s_queue[index].used) {
            return &s_queue[index];
        }
    }
    return NULL;
}

esp_err_t motion_init(void) {
    motion_restore();
    motion_drop_queue();
    s_action = MOTION_ACTION_IDLE;
    s_state = MOTION_STATE_IDLE;
    s_ready = true;
    return ESP_OK;
}

bool motion_is_ready(void) {
    return s_ready;
}

esp_err_t motion_apply(const motion_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !motion_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    // The bounded policy is the execution contract; the driver only reads
    // validated fields from here.
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t motion_set(const motion_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = motion_clamp(policy);
    return motion_persist();
}

motion_policy_t motion_get(void) {
    return s_policy;
}

size_t motion_queue_depth(void) {
    return s_queue_depth;
}

bool motion_queued_for(uint8_t actuator) {
    return motion_find_slot(actuator) != NULL;
}

esp_err_t motion_submit(const motion_command_t *command) {
    if (!s_ready) {
        return ESP_ERR_NOT_READY;
    }
    if (command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // A latched fault rejects every new action until motion_clear runs.
    if (s_state == MOTION_STATE_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_policy.enabled == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!motion_actuator_valid(command->actuator)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (motion_action_is_locomotion(command->action) &&
        s_policy.allow_locomotion == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    const motion_command_t bounded = motion_clamp_command(&s_policy, command);
    motion_slot_t *slot = motion_find_slot(bounded.actuator);
    if (slot == NULL) {
        slot = motion_free_slot();
    }
    if (slot == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!slot->used) {
        s_queue_depth += 1u;
    }
    slot->used = true;
    slot->actuator = bounded.actuator;
    slot->action = bounded.action;
    slot->angle_degrees = bounded.angle_degrees;
    slot->speed_percent = bounded.speed_percent;
    return ESP_OK;
}

esp_err_t motion_start(motion_action_t action) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_policy.enabled == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    if (motion_action_is_locomotion(action) &&
        s_policy.allow_locomotion == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    // A fault latches the controller; only motion_clear releases it.
    if (s_state == MOTION_STATE_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }
    s_action = action;
    s_state = MOTION_STATE_RUNNING;
    return ESP_OK;
}

motion_state_t motion_tick(uint32_t elapsed_ms) {
    if (!s_ready) {
        return MOTION_STATE_IDLE;
    }
    if (s_state == MOTION_STATE_FAULT) {
        return s_state;
    }
    // A latched stop or timeout only changes through motion_clear, so a later
    // tick cannot silently resume a stopped action.
    if (s_state == MOTION_STATE_STOPPED || s_state == MOTION_STATE_TIMEOUT) {
        return s_state;
    }
    if (s_state != MOTION_STATE_RUNNING) {
        return s_state;
    }
    if (motion_runtime_exceeded(&s_policy, elapsed_ms)) {
        s_state = MOTION_STATE_TIMEOUT;
        s_action = MOTION_ACTION_IDLE;
        motion_drop_queue();
        return s_state;
    }
    return s_state;
}

bool motion_request_stop(void) {
    if (!s_ready || s_state == MOTION_STATE_FAULT) {
        return false;
    }
    // A pending timeout keeps its latch; an explicit stop only supersedes a
    // running action, never a latched fault.
    if (s_state == MOTION_STATE_RUNNING) {
        s_state = MOTION_STATE_STOPPED;
        s_action = MOTION_ACTION_IDLE;
        motion_drop_queue();
    }
    return true;
}

esp_err_t motion_clear(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    motion_drop_queue();
    s_action = MOTION_ACTION_IDLE;
    s_state = MOTION_STATE_IDLE;
    return ESP_OK;
}

esp_err_t motion_fault(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    motion_drop_queue();
    s_action = MOTION_ACTION_IDLE;
    s_state = MOTION_STATE_FAULT;
    return ESP_OK;
}

motion_state_t motion_state(void) {
    return s_state;
}

bool motion_should_stop_on_disconnect(const motion_policy_t *policy) {
    const motion_policy_t bounded = motion_clamp(policy);
    if (bounded.enabled == 0u) {
        return false;
    }
    // Locomotion always stops on a lost link; a non-locomotion action stops
    // only when the policy requires an acknowledgement that can no longer
    // arrive.
    if (motion_action_is_locomotion(s_action)) {
        return true;
    }
    return bounded.require_stop_ack != 0u;
}

const char *motion_error_name(motion_error_t error) {
    switch (error) {
        case MOTION_OK:
            return "ok";
        case MOTION_ERR_NULL_ARGUMENT:
            return "null_argument";
        case MOTION_ERR_INVALID_POLICY:
            return "invalid_policy";
        case MOTION_ERR_INVALID_COMMAND:
            return "invalid_command";
        case MOTION_ERR_STORAGE:
            return "storage";
        case MOTION_ERR_NOT_READY:
            return "not_ready";
        default:
            return "unknown";
    }
}

const char *motion_action_name(motion_action_t action) {
    switch (action) {
        case MOTION_ACTION_WALK:
            return "walk";
        case MOTION_ACTION_DANCE:
            return "dance";
        case MOTION_ACTION_TAIL_WAG:
            return "tail_wag";
        case MOTION_ACTION_GESTURE:
            return "gesture";
        case MOTION_ACTION_IDLE:
        default:
            return "idle";
    }
}

const char *motion_state_name(motion_state_t state) {
    switch (state) {
        case MOTION_STATE_RUNNING:
            return "running";
        case MOTION_STATE_STOPPED:
            return "stopped";
        case MOTION_STATE_TIMEOUT:
            return "timeout";
        case MOTION_STATE_FAULT:
            return "fault";
        case MOTION_STATE_IDLE:
        default:
            return "idle";
    }
}

static esp_err_t motion_initialize(void) {
    return motion_init();
}

const module_descriptor_t *motion_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "motion_control",
        .version = "1.0.0",
        .initialize = motion_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
