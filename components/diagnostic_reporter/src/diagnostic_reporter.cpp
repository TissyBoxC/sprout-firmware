#include "diagnostic_reporter.h"

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
#define DIAGNOSTIC_REPORTER_STATE_MAGIC 0x53444731u
#define DIAGNOSTIC_REPORTER_STATE_VERSION 3u
#define DIAGNOSTIC_REPORTER_MAX_STATE_SIZE 2048u

static_assert(
    sizeof(DIAGNOSTIC_REPORTER_NVS_KEY) <= NVS_KEY_NAME_MAX_SIZE,
    "diagnostic state key must fit the ESP-IDF NVS key limit"
);

// Failure and recovery records keep the source transition counters so a
// reboot or a repeated observation cannot create a second diagnostic event.
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
} diagnostic_reporter_state_t;

static_assert(
    sizeof(diagnostic_reporter_state_t) <= DIAGNOSTIC_REPORTER_MAX_STATE_SIZE,
    "diagnostic state must remain a bounded NVS blob"
);

static const char *const TAG = "diagnostic_reporter";
static bool diagnostic_reporter_ready;
static SemaphoreHandle_t diagnostic_reporter_mutex;
static diagnostic_reporter_state_t diagnostic_reporter_state;

static bool diagnostic_reporter_lock(void) {
    return diagnostic_reporter_mutex != NULL &&
           xSemaphoreTake(diagnostic_reporter_mutex, portMAX_DELAY) == pdTRUE;
}

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
    size_t state_size = sizeof(diagnostic_reporter_state);
    const esp_err_t result = config_store_get_blob(
        DIAGNOSTIC_REPORTER_NVS_KEY,
        &diagnostic_reporter_state,
        &state_size
    );
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
    if (state_size != sizeof(diagnostic_reporter_state) ||
        diagnostic_reporter_state.magic != DIAGNOSTIC_REPORTER_STATE_MAGIC ||
        diagnostic_reporter_state.version != DIAGNOSTIC_REPORTER_STATE_VERSION ||
        diagnostic_reporter_state.boot_event_count >
            DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY ||
        diagnostic_reporter_state.recovery_event_count >
            DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY ||
        diagnostic_reporter_state.source_boot_count >
            diagnostic_reporter_state.boot_count ||
        diagnostic_reporter_state.failure.pending > 1) {
        ESP_LOGW(TAG, "stored diagnostic state is invalid; resetting");
        diagnostic_reporter_clear_state_locked();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
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
    return newest;
}

static esp_err_t diagnostic_reporter_record_failure_locked(
    const error_recovery_snapshot_t *failure
) {
    diagnostic_reporter_failure_state_t next_failure = {};
    const diagnostic_reporter_failure_state_t previous_failure =
        diagnostic_reporter_state.failure;
    const uint32_t previous_next_sequence =
        diagnostic_reporter_state.next_sequence;

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
        diagnostic_reporter_state.failure = previous_failure;
        diagnostic_reporter_state.next_sequence = previous_next_sequence;
    }
    return result;
}

static esp_err_t diagnostic_reporter_record_recovery_locked(
    const error_recovery_recovery_t *recovery
) {
    diagnostic_reporter_recovery_state_t event = {};
    const uint32_t previous_next_sequence =
        diagnostic_reporter_state.next_sequence;
    const uint16_t previous_event_count =
        diagnostic_reporter_state.recovery_event_count;

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
        diagnostic_reporter_state.recovery_event_count =
            previous_event_count;
        diagnostic_reporter_state.next_sequence = previous_next_sequence;
    }
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
        result = diagnostic_reporter_save_state_locked();
        if (result != ESP_OK) {
            diagnostic_reporter_state.source_boot_count = 0;
            diagnostic_reporter_state.last_failure_transition_count = 0;
            diagnostic_reporter_state.last_recovery_transition_count = 0;
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

    size_t retained_count = 0;
    for (size_t index = 0;
         index < diagnostic_reporter_state.boot_event_count;
         ++index) {
        const diagnostic_reporter_boot_event_t *event =
            &diagnostic_reporter_state.boot_events[index];
        if (event->sequence > through_sequence) {
            diagnostic_reporter_state.boot_events[retained_count++] = *event;
        }
    }
    diagnostic_reporter_state.boot_event_count = retained_count;

    size_t retained_recovery_count = 0;
    for (size_t index = 0;
         index < diagnostic_reporter_state.recovery_event_count;
         ++index) {
        const diagnostic_reporter_recovery_state_t *event =
            &diagnostic_reporter_state.recovery_events[index];
        if (event->sequence > through_sequence) {
            diagnostic_reporter_state.recovery_events
                [retained_recovery_count++] = *event;
        }
    }
    diagnostic_reporter_state.recovery_event_count =
        retained_recovery_count;

    if (diagnostic_reporter_state.failure.pending &&
        diagnostic_reporter_state.failure.sequence <= through_sequence) {
        // Keep the delivered identity so the same source transition is not
        // emitted again on the next heartbeat.
        diagnostic_reporter_state.failure.pending = 0;
    }

    const esp_err_t result = diagnostic_reporter_save_state_locked();
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
