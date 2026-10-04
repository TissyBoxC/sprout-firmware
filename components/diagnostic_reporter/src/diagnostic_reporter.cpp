#include "diagnostic_reporter.h"
#include "diagnostic_reporter_state.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "error_recovery.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "version_info.h"

#define DIAGNOSTIC_REPORTER_NVS_KEY "diag_state"

static_assert(
    sizeof(DIAGNOSTIC_REPORTER_NVS_KEY) <= NVS_KEY_NAME_MAX_SIZE,
    "diagnostic state key must fit the ESP-IDF NVS key limit"
);

static_assert(
    sizeof(diagnostic_reporter_state_t) <= DIAGNOSTIC_REPORTER_MAX_STATE_SIZE,
    "diagnostic state must remain a bounded NVS blob"
);
static_assert(
    sizeof(diagnostic_reporter_state_v3_t) <=
        DIAGNOSTIC_REPORTER_MAX_STATE_SIZE,
    "v3 diagnostic state must remain a bounded NVS blob"
);
static_assert(
    sizeof(diagnostic_reporter_state_v4_t) <=
        DIAGNOSTIC_REPORTER_MAX_STATE_SIZE,
    "v4 diagnostic state must remain a bounded NVS blob"
);

static const char *const TAG = "diagnostic_reporter";
static bool diagnostic_reporter_ready;
static SemaphoreHandle_t diagnostic_reporter_mutex;
static diagnostic_reporter_state_t diagnostic_reporter_state;

static bool diagnostic_reporter_lock(void) {
    return diagnostic_reporter_mutex != NULL &&
           xSemaphoreTake(diagnostic_reporter_mutex, portMAX_DELAY) == pdTRUE;
}

static esp_err_t diagnostic_reporter_save_state_locked(void);

static void diagnostic_reporter_unlock(void) {
    if (diagnostic_reporter_mutex != NULL) {
        (void)xSemaphoreGive(diagnostic_reporter_mutex);
    }
}

static void diagnostic_reporter_copy_text(
    char *output,
    size_t output_size,
    const char *input
) {
    if (output == NULL || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == NULL) {
        return;
    }

    size_t output_index = 0;
    while (input[output_index] != '\0' &&
           output_index + 1 < output_size) {
        const unsigned char value = (unsigned char)input[output_index];
        output[output_index] =
            value >= 0x20 && value <= 0x7e ? (char)value : '_';
        ++output_index;
    }
    output[output_index] = '\0';
}

static const char *diagnostic_reporter_reset_reason(void) {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
            return "task_watchdog";
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power_glitch";
        case ESP_RST_CPU_LOCKUP:
            return "cpu_lockup";
        case ESP_RST_UNKNOWN:
        default:
            return "unknown";
    }
}

static uint32_t diagnostic_reporter_next_sequence_locked(void) {
    uint32_t sequence = diagnostic_reporter_state.next_sequence;
    if (sequence == 0 || sequence == UINT32_MAX) {
        sequence = 1;
    }
    diagnostic_reporter_state.next_sequence = sequence + 1;
    return sequence;
}

static void diagnostic_reporter_clear_state_locked(void) {
    memset(&diagnostic_reporter_state, 0, sizeof(diagnostic_reporter_state));
    diagnostic_reporter_state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    diagnostic_reporter_state.version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    diagnostic_reporter_state.next_sequence = 1;
}

static esp_err_t diagnostic_reporter_load_state_locked(void) {
    // Probe into a separate buffer first so a different on-flash layout is
    // never reinterpreted as the current v5 structure.
    uint8_t raw_state[sizeof(diagnostic_reporter_state)] = {};
    size_t state_size = sizeof(raw_state);
    esp_err_t result = config_store_get_blob(
        DIAGNOSTIC_REPORTER_NVS_KEY,
        raw_state,
        &state_size
    );
    if (result == ESP_ERR_NVS_INVALID_LENGTH) {
        // config_store reports the required size. A legacy v3/v4 record is
        // bounded and safe to read once into the current maximum buffer.
        if (state_size == 0 || state_size > sizeof(raw_state)) {
            ESP_LOGW(
                TAG,
                "stored diagnostic state is too large (%u); resetting",
                (unsigned)state_size
            );
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        result = config_store_get_blob(
            DIAGNOSTIC_REPORTER_NVS_KEY,
            raw_state,
            &state_size
        );
    }
    if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        return result;
    }
    if (result != ESP_OK) {
        // A record written by a different firmware layout is not usable, but
        // other NVS errors are real storage failures and must remain visible.
        if (result == ESP_ERR_NVS_INVALID_LENGTH) {
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        return result;
    }
    if (state_size < sizeof(uint16_t) + sizeof(uint32_t)) {
        ESP_LOGW(TAG, "stored diagnostic state is truncated; resetting");
        diagnostic_reporter_clear_state_locked();
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t magic = 0;
    uint16_t version = 0;
    memcpy(&magic, raw_state, sizeof(magic));
    memcpy(&version, raw_state + sizeof(magic), sizeof(version));
    if (magic != DIAGNOSTIC_REPORTER_STATE_MAGIC) {
        ESP_LOGW(TAG, "stored diagnostic state has unknown magic; resetting");
        diagnostic_reporter_clear_state_locked();
        return ESP_ERR_INVALID_STATE;
    }

    if (state_size == sizeof(diagnostic_reporter_state_t) &&
        version == DIAGNOSTIC_REPORTER_STATE_VERSION) {
        memcpy(&diagnostic_reporter_state, raw_state, state_size);
        if (!diagnostic_reporter_v5_is_valid(&diagnostic_reporter_state)) {
            ESP_LOGW(TAG, "stored diagnostic state is invalid; resetting");
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        return ESP_OK;
    }

    if (state_size == sizeof(diagnostic_reporter_state_v3_t) &&
        version == DIAGNOSTIC_REPORTER_STATE_VERSION_V3) {
        diagnostic_reporter_state_v3_t old_state = {};
        memcpy(&old_state, raw_state, sizeof(old_state));
        if (!diagnostic_reporter_v3_is_valid(&old_state)) {
            ESP_LOGW(TAG, "stored v3 diagnostic state is invalid; resetting");
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        diagnostic_reporter_state_t migrated = {};
        diagnostic_reporter_migrate_v3(&migrated, &old_state);
        if (!diagnostic_reporter_v5_is_valid(&migrated)) {
            ESP_LOGW(TAG, "migrated v3 diagnostic state is invalid; resetting");
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        diagnostic_reporter_state = migrated;
        const esp_err_t save_result =
            diagnostic_reporter_save_state_locked();
        if (save_result != ESP_OK) {
            diagnostic_reporter_clear_state_locked();
            return save_result;
        }
        ESP_LOGI(TAG, "migrated diagnostic state from v3 to v5");
        return ESP_OK;
    }

    if (state_size == sizeof(diagnostic_reporter_state_v4_t) &&
        version == DIAGNOSTIC_REPORTER_STATE_VERSION_V4) {
        diagnostic_reporter_state_v4_t old_state = {};
        memcpy(&old_state, raw_state, sizeof(old_state));
        if (!diagnostic_reporter_v4_is_valid(&old_state)) {
            ESP_LOGW(TAG, "stored v4 diagnostic state is invalid; resetting");
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        diagnostic_reporter_state_t migrated = {};
        diagnostic_reporter_migrate_v4(&migrated, &old_state);
        if (!diagnostic_reporter_v5_is_valid(&migrated)) {
            ESP_LOGW(TAG, "migrated v4 diagnostic state is invalid; resetting");
            diagnostic_reporter_clear_state_locked();
            return ESP_ERR_INVALID_STATE;
        }
        diagnostic_reporter_state = migrated;
        const esp_err_t save_result =
            diagnostic_reporter_save_state_locked();
        if (save_result != ESP_OK) {
            diagnostic_reporter_clear_state_locked();
            return save_result;
        }
        ESP_LOGI(TAG, "migrated diagnostic state from v4 to v5");
        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "stored diagnostic state has unsupported size/version (%u/%u); resetting",
        (unsigned)state_size,
        (unsigned)version
    );
    diagnostic_reporter_clear_state_locked();
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t diagnostic_reporter_save_state_locked(void) {
    return config_store_set_blob(
        DIAGNOSTIC_REPORTER_NVS_KEY,
        &diagnostic_reporter_state,
        sizeof(diagnostic_reporter_state)
    );
}

static uint32_t diagnostic_reporter_uptime_ms(void) {
    const int64_t uptime_us = esp_timer_get_time();
    if (uptime_us <= 0) {
        return 0;
    }
    const uint64_t uptime_ms = (uint64_t)uptime_us / 1000u;
    return uptime_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)uptime_ms;
}

static void diagnostic_reporter_make_event_id(
    char *output,
    size_t output_size,
    uint32_t boot_count
) {
    snprintf(
        output,
        output_size,
        "boot_%08lx%08lx%08lx",
        (unsigned long)esp_random(),
        (unsigned long)esp_random(),
        (unsigned long)boot_count
    );
}

static void diagnostic_reporter_make_recovery_event_id(
    char *output,
    size_t output_size,
    uint32_t sequence
) {
    snprintf(
        output,
        output_size,
        "recovery_%08lx",
        (unsigned long)sequence
    );
}

static void diagnostic_reporter_append_boot_event_locked(
    const diagnostic_reporter_boot_event_t *event
) {
    if (diagnostic_reporter_state.boot_event_count ==
        DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY) {
        memmove(
            &diagnostic_reporter_state.boot_events[0],
            &diagnostic_reporter_state.boot_events[1],
            sizeof(diagnostic_reporter_state.boot_events[0]) *
                (DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY - 1)
        );
        --diagnostic_reporter_state.boot_event_count;
        ++diagnostic_reporter_state.dropped_boot_events;
    }
    diagnostic_reporter_state.boot_events
        [diagnostic_reporter_state.boot_event_count++] = *event;
}

static void diagnostic_reporter_append_recovery_event_locked(
    const diagnostic_reporter_recovery_state_t *event
) {
    if (diagnostic_reporter_state.recovery_event_count ==
        DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY) {
        memmove(
            &diagnostic_reporter_state.recovery_events[0],
            &diagnostic_reporter_state.recovery_events[1],
            sizeof(diagnostic_reporter_state.recovery_events[0]) *
                (DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY - 1)
        );
        --diagnostic_reporter_state.recovery_event_count;
    }
    diagnostic_reporter_state.recovery_events
        [diagnostic_reporter_state.recovery_event_count++] = *event;
}

static void diagnostic_reporter_append_interaction_event_locked(
    const diagnostic_reporter_interaction_state_t *event
) {
    if (diagnostic_reporter_state.interaction_event_count ==
        DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY) {
        memmove(
            &diagnostic_reporter_state.interaction_events[0],
            &diagnostic_reporter_state.interaction_events[1],
            sizeof(diagnostic_reporter_state.interaction_events[0]) *
                (DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY - 1)
        );
        --diagnostic_reporter_state.interaction_event_count;
    }
    diagnostic_reporter_state.interaction_events
        [diagnostic_reporter_state.interaction_event_count++] = *event;
}

static uint32_t diagnostic_reporter_newest_sequence_locked(void) {
    uint32_t newest = 0;
    for (size_t index = 0;
         index < diagnostic_reporter_state.boot_event_count;
         ++index) {
        if (diagnostic_reporter_state.boot_events[index].sequence > newest) {
            newest = diagnostic_reporter_state.boot_events[index].sequence;
        }
    }
    if (diagnostic_reporter_state.failure.pending &&
        diagnostic_reporter_state.failure.sequence > newest) {
        newest = diagnostic_reporter_state.failure.sequence;
    }
    for (size_t index = 0;
         index < diagnostic_reporter_state.recovery_event_count;
         ++index) {
        const uint32_t sequence =
            diagnostic_reporter_state.recovery_events[index].sequence;
        if (sequence > newest) {
            newest = sequence;
        }
    }
    for (size_t index = 0;
         index < diagnostic_reporter_state.interaction_event_count;
         ++index) {
        const uint32_t sequence =
            diagnostic_reporter_state.interaction_events[index].sequence;
        if (sequence > newest) {
            newest = sequence;
        }
    }
    return newest;
}

static esp_err_t diagnostic_reporter_record_failure_locked(
    const error_recovery_snapshot_t *failure
) {
    diagnostic_reporter_failure_state_t next_failure = {};
    const diagnostic_reporter_state_t previous_state =
        diagnostic_reporter_state;

    next_failure.sequence = diagnostic_reporter_next_sequence_locked();
    next_failure.failure_count = failure->failure_count;
    next_failure.source_transition_count = failure->transition_count;
    next_failure.pending = 1;
    diagnostic_reporter_copy_text(
        next_failure.module_name,
        sizeof(next_failure.module_name),
        failure->module_name
    );
    diagnostic_reporter_copy_text(
        next_failure.error_code,
        sizeof(next_failure.error_code),
        esp_err_to_name(failure->error_code)
    );
    const version_info_t version = version_info_get();
    diagnostic_reporter_copy_text(
        next_failure.firmware_version,
        sizeof(next_failure.firmware_version),
        version.firmware_version
    );
    diagnostic_reporter_state.failure = next_failure;

    const esp_err_t result = diagnostic_reporter_save_state_locked();
    if (result != ESP_OK) {
        diagnostic_reporter_state = previous_state;
    }
    return result;
}

static esp_err_t diagnostic_reporter_record_recovery_locked(
    const error_recovery_recovery_t *recovery
) {
    diagnostic_reporter_recovery_state_t event = {};
    const diagnostic_reporter_state_t previous_state =
        diagnostic_reporter_state;

    event.sequence = diagnostic_reporter_next_sequence_locked();
    event.source_transition_count = recovery->transition_count;
    diagnostic_reporter_make_recovery_event_id(
        event.event_id,
        sizeof(event.event_id),
        event.sequence
    );
    diagnostic_reporter_copy_text(
        event.module_name,
        sizeof(event.module_name),
        recovery->module_name
    );
    const version_info_t version = version_info_get();
    diagnostic_reporter_copy_text(
        event.firmware_version,
        sizeof(event.firmware_version),
        version.firmware_version
    );

    diagnostic_reporter_append_recovery_event_locked(&event);
    const esp_err_t result = diagnostic_reporter_save_state_locked();
    if (result != ESP_OK) {
        diagnostic_reporter_state = previous_state;
    }
    return result;
}

esp_err_t diagnostic_reporter_record_interaction(
    const char *event_type,
    const char *detail_code,
    uint32_t duration_ms
) {
    if (!diagnostic_reporter_ready ||
        detail_code == NULL ||
        detail_code[0] == '\0' ||
        duration_ms > 3600000u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!diagnostic_reporter_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    diagnostic_reporter_interaction_state_t event = {};
    const diagnostic_reporter_state_t previous_state =
        diagnostic_reporter_state;

    event.sequence = diagnostic_reporter_next_sequence_locked();
    event.duration_ms = duration_ms;
    snprintf(
        event.event_id,
        sizeof(event.event_id),
        "interaction_%08lx",
        (unsigned long)event.sequence
    );
    diagnostic_reporter_copy_text(
        event.event_type,
        sizeof(event.event_type),
        event_type
    );
    diagnostic_reporter_copy_text(
        event.detail_code,
        sizeof(event.detail_code),
        detail_code
    );
    const version_info_t version = version_info_get();
    diagnostic_reporter_copy_text(
        event.firmware_version,
        sizeof(event.firmware_version),
        version.firmware_version
    );

    // Reuse the persisted-state validator so live producers cannot introduce
    // event types, detail codes, or field combinations that a restored device
    // would reject later. The candidate append is rolled back if invalid.
    diagnostic_reporter_append_interaction_event_locked(&event);
    if (!diagnostic_reporter_v5_is_valid(&diagnostic_reporter_state)) {
        diagnostic_reporter_state = previous_state;
        diagnostic_reporter_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    diagnostic_reporter_state = previous_state;

    diagnostic_reporter_append_interaction_event_locked(&event);
    const esp_err_t result = diagnostic_reporter_save_state_locked();
    if (result != ESP_OK) {
        diagnostic_reporter_state = previous_state;
    }
    diagnostic_reporter_unlock();
    return result;
}

// Convert source transitions into diagnostic events in true occurrence
// order. Both counters come from error_recovery, so a failure followed by a
// recovery in the same heartbeat still yields the correct health state.
static esp_err_t diagnostic_reporter_observe_transitions_locked(void) {
    const bool source_boot_changed =
        diagnostic_reporter_state.source_boot_count !=
        diagnostic_reporter_state.boot_count;
    if (source_boot_changed) {
        diagnostic_reporter_state.source_boot_count =
            diagnostic_reporter_state.boot_count;
        diagnostic_reporter_state.last_failure_transition_count = 0;
        diagnostic_reporter_state.last_recovery_transition_count = 0;
    }

    const error_recovery_snapshot_t failure =
        error_recovery_get_snapshot();
    const error_recovery_recovery_snapshot_t recoveries =
        error_recovery_get_recoveries();

    bool failure_pending =
        failure.module_name[0] != '\0' &&
        failure.error_code != ESP_OK &&
        failure.transition_count != 0 &&
        failure.transition_count >
            diagnostic_reporter_state.last_failure_transition_count;
    size_t recovery_index = 0;
    while (recovery_index < recoveries.event_count &&
           recoveries.events[recovery_index].transition_count <=
               diagnostic_reporter_state.last_recovery_transition_count) {
        ++recovery_index;
    }

    esp_err_t result = ESP_OK;
    bool changed = false;
    while (result == ESP_OK) {
        const bool has_recovery = recovery_index < recoveries.event_count;
        const uint32_t recovery_transition =
            has_recovery
                ? recoveries.events[recovery_index].transition_count
                : UINT32_MAX;
        const uint32_t failure_transition =
            failure_pending ? failure.transition_count : UINT32_MAX;
        if (recovery_transition == UINT32_MAX &&
            failure_transition == UINT32_MAX) {
            break;
        }

        if (failure_transition <= recovery_transition) {
            const uint32_t previous_transition =
                diagnostic_reporter_state
                    .last_failure_transition_count;
            diagnostic_reporter_state.last_failure_transition_count =
                failure_transition;
            result = diagnostic_reporter_record_failure_locked(&failure);
            if (result != ESP_OK) {
                diagnostic_reporter_state.last_failure_transition_count =
                    previous_transition;
                break;
            }
            failure_pending = false;
            changed = true;
            continue;
        }

        const uint32_t previous_transition =
            diagnostic_reporter_state.last_recovery_transition_count;
        diagnostic_reporter_state.last_recovery_transition_count =
            recovery_transition;
        result = diagnostic_reporter_record_recovery_locked(
            &recoveries.events[recovery_index]
        );
        if (result != ESP_OK) {
            diagnostic_reporter_state.last_recovery_transition_count =
                previous_transition;
            break;
        }
        ++recovery_index;
        changed = true;
    }

    if (result == ESP_OK && source_boot_changed && !changed) {
        const diagnostic_reporter_state_t previous_state =
            diagnostic_reporter_state;
        result = diagnostic_reporter_save_state_locked();
        if (result != ESP_OK) {
            diagnostic_reporter_state = previous_state;
        }
    }
    return result;
}

esp_err_t diagnostic_reporter_init(void) {
    if (diagnostic_reporter_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    diagnostic_reporter_mutex = xSemaphoreCreateMutex();
    if (diagnostic_reporter_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!diagnostic_reporter_lock()) {
        vSemaphoreDelete(diagnostic_reporter_mutex);
        diagnostic_reporter_mutex = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = diagnostic_reporter_load_state_locked();
    if (result == CONFIG_STORE_ERR_NOT_FOUND ||
        result == ESP_ERR_INVALID_STATE) {
        if (result == CONFIG_STORE_ERR_NOT_FOUND) {
            diagnostic_reporter_clear_state_locked();
        }
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        diagnostic_reporter_state.boot_count =
            diagnostic_reporter_state.boot_count == UINT32_MAX
                ? UINT32_MAX
                : diagnostic_reporter_state.boot_count + 1;
        diagnostic_reporter_state.source_boot_count =
            diagnostic_reporter_state.boot_count;
        diagnostic_reporter_state.last_failure_transition_count = 0;
        diagnostic_reporter_state.last_recovery_transition_count = 0;

        diagnostic_reporter_boot_event_t event = {};
        event.sequence = diagnostic_reporter_next_sequence_locked();
        event.uptime_ms = diagnostic_reporter_uptime_ms();
        event.boot_count = diagnostic_reporter_state.boot_count;
        diagnostic_reporter_make_event_id(
            event.event_id,
            sizeof(event.event_id),
            event.boot_count
        );
        diagnostic_reporter_copy_text(
            event.reset_reason,
            sizeof(event.reset_reason),
            diagnostic_reporter_reset_reason()
        );
        const version_info_t version = version_info_get();
        diagnostic_reporter_copy_text(
            event.firmware_version,
            sizeof(event.firmware_version),
            version.firmware_version
        );
        diagnostic_reporter_append_boot_event_locked(&event);
        result = diagnostic_reporter_save_state_locked();
    }

    diagnostic_reporter_unlock();
    if (result != ESP_OK) {
        vSemaphoreDelete(diagnostic_reporter_mutex);
        diagnostic_reporter_mutex = NULL;
        memset(&diagnostic_reporter_state, 0, sizeof(diagnostic_reporter_state));
        return result;
    }
    diagnostic_reporter_ready = true;
    return ESP_OK;
}

esp_err_t diagnostic_reporter_get_snapshot(
    diagnostic_reporter_snapshot_t *snapshot
) {
    if (!diagnostic_reporter_ready || snapshot == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!diagnostic_reporter_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = diagnostic_reporter_observe_transitions_locked();
    if (result == ESP_OK) {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->boot_event_count =
            diagnostic_reporter_state.boot_event_count;
        for (size_t index = 0;
             index < diagnostic_reporter_state.boot_event_count;
             ++index) {
            snapshot->boot_events[index] =
                diagnostic_reporter_state.boot_events[index];
        }
        snapshot->newest_sequence =
            diagnostic_reporter_newest_sequence_locked();
        snapshot->dropped_boot_events =
            diagnostic_reporter_state.dropped_boot_events;
        snapshot->recovery_event_count =
            diagnostic_reporter_state.recovery_event_count;
        for (size_t index = 0;
             index < diagnostic_reporter_state.recovery_event_count;
             ++index) {
            const diagnostic_reporter_recovery_state_t *event =
                &diagnostic_reporter_state.recovery_events[index];
            snapshot->recovery_events[index].sequence = event->sequence;
            diagnostic_reporter_copy_text(
                snapshot->recovery_events[index].event_id,
                sizeof(snapshot->recovery_events[index].event_id),
                event->event_id
            );
            diagnostic_reporter_copy_text(
                snapshot->recovery_events[index].module_name,
                sizeof(snapshot->recovery_events[index].module_name),
                event->module_name
            );
            diagnostic_reporter_copy_text(
                snapshot->recovery_events[index].firmware_version,
                sizeof(snapshot->recovery_events[index].firmware_version),
                event->firmware_version
            );
        }
        snapshot->interaction_event_count =
            diagnostic_reporter_state.interaction_event_count;
        for (size_t index = 0;
             index < diagnostic_reporter_state.interaction_event_count;
             ++index) {
            const diagnostic_reporter_interaction_state_t *event =
                &diagnostic_reporter_state.interaction_events[index];
            snapshot->interaction_events[index].sequence = event->sequence;
            snapshot->interaction_events[index].duration_ms =
                event->duration_ms;
            diagnostic_reporter_copy_text(
                snapshot->interaction_events[index].event_id,
                sizeof(snapshot->interaction_events[index].event_id),
                event->event_id
            );
            diagnostic_reporter_copy_text(
                snapshot->interaction_events[index].event_type,
                sizeof(snapshot->interaction_events[index].event_type),
                event->event_type
            );
            diagnostic_reporter_copy_text(
                snapshot->interaction_events[index].detail_code,
                sizeof(snapshot->interaction_events[index].detail_code),
                event->detail_code
            );
            diagnostic_reporter_copy_text(
                snapshot->interaction_events[index].firmware_version,
                sizeof(
                    snapshot->interaction_events[index].firmware_version
                ),
                event->firmware_version
            );
        }
        snapshot->has_failure =
            diagnostic_reporter_state.failure.pending != 0;
        if (snapshot->has_failure) {
            snapshot->failure.sequence =
                diagnostic_reporter_state.failure.sequence;
            snapshot->failure.failure_count =
                diagnostic_reporter_state.failure.failure_count;
            diagnostic_reporter_copy_text(
                snapshot->failure.module_name,
                sizeof(snapshot->failure.module_name),
                diagnostic_reporter_state.failure.module_name
            );
            diagnostic_reporter_copy_text(
                snapshot->failure.error_code,
                sizeof(snapshot->failure.error_code),
                diagnostic_reporter_state.failure.error_code
            );
            diagnostic_reporter_copy_text(
                snapshot->failure.firmware_version,
                sizeof(snapshot->failure.firmware_version),
                diagnostic_reporter_state.failure.firmware_version
            );
        }
    }

    diagnostic_reporter_unlock();
    return result;
}

esp_err_t diagnostic_reporter_acknowledge(uint32_t through_sequence) {
    if (!diagnostic_reporter_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (through_sequence == 0) {
        return ESP_OK;
    }
    if (!diagnostic_reporter_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    const diagnostic_reporter_state_t previous_state =
        diagnostic_reporter_state;
    (void)diagnostic_reporter_acknowledge_state(
        &diagnostic_reporter_state,
        through_sequence
    );

    const esp_err_t result = diagnostic_reporter_save_state_locked();
    if (result != ESP_OK) {
        diagnostic_reporter_state = previous_state;
    }
    diagnostic_reporter_unlock();
    return result;
}

void diagnostic_reporter_shutdown(void) {
    if (diagnostic_reporter_mutex != NULL) {
        vSemaphoreDelete(diagnostic_reporter_mutex);
        diagnostic_reporter_mutex = NULL;
    }
    diagnostic_reporter_ready = false;
}

const module_descriptor_t *diagnostic_reporter_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "diagnostic_reporter",
        .version = "1.0.0",
        .initialize = diagnostic_reporter_init,
        .shutdown = diagnostic_reporter_shutdown,
    };
    return &descriptor;
}
