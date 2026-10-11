#include "alarm_reminder.h"

#include <stddef.h>
#include <string.h>

#include "alarm_reminder_core.h"
#include "config_store.h"

// The bounded alarm store persists as one binary blob so the device keeps the
// guardian's schedule and local firing state across restarts without a
// dedicated partition.
#define ALARM_REMINDER_STORE_KEY "alarm_reminder_index"

static alarm_reminder_core_t s_core;
static bool s_ready;

static esp_err_t alarm_reminder_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        ALARM_REMINDER_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void alarm_reminder_restore(void) {
    alarm_reminder_core_t restored = {0};
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        ALARM_REMINDER_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= ALARM_REMINDER_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid store so a
    // corrupt store never blocks startup.
    alarm_reminder_core_init(&s_core);
}

esp_err_t alarm_reminder_init(void) {
    alarm_reminder_core_init(&s_core);
    alarm_reminder_restore();
    s_ready = true;
    return ESP_OK;
}

bool alarm_reminder_is_ready(void) {
    return s_ready;
}

esp_err_t alarm_reminder_upsert(const alarm_reminder_t *alarm) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const alarm_reminder_error_t result = alarm_reminder_core_upsert(
        &s_core,
        alarm
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

esp_err_t alarm_reminder_remove(const char *alarm_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const alarm_reminder_error_t result = alarm_reminder_core_remove(
        &s_core,
        alarm_id
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

esp_err_t alarm_reminder_remove_child(
    const char *child_id,
    size_t *removed_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const alarm_reminder_error_t result = alarm_reminder_core_remove_child(
        &s_core,
        child_id,
        removed_out
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

esp_err_t alarm_reminder_list(
    alarm_reminder_t *alarms_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (alarms_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        alarms_out[index] = s_core.alarms[index];
    }
    *count_out = s_core.count;
    return ESP_OK;
}

bool alarm_reminder_get(const char *alarm_id, alarm_reminder_t *alarm_out) {
    if (alarm_id == NULL || alarm_out == NULL) {
        return false;
    }
    const size_t index = alarm_reminder_core_find(&s_core, alarm_id);
    if (index >= s_core.count) {
        return false;
    }
    *alarm_out = s_core.alarms[index];
    return true;
}

size_t alarm_reminder_count(void) {
    return s_core.count;
}

size_t alarm_reminder_due_at(
    int weekday,
    uint16_t minute_of_day,
    int32_t local_date,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
) {
    return alarm_reminder_core_due_at(
        &s_core,
        weekday,
        minute_of_day,
        local_date,
        due_ids_out,
        capacity
    );
}

size_t alarm_reminder_due_snoozes(
    int64_t now_tick,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
) {
    return alarm_reminder_core_due_snoozes(
        &s_core,
        now_tick,
        due_ids_out,
        capacity
    );
}

int64_t alarm_reminder_tick(int32_t local_date, uint16_t minute_of_day) {
    return alarm_reminder_core_tick(local_date, minute_of_day);
}

esp_err_t alarm_reminder_mark_ringing(
    const char *alarm_id,
    int32_t local_date
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t index = alarm_reminder_core_find(&s_core, alarm_id);
    if (index >= s_core.count) {
        return ESP_ERR_INVALID_ARG;
    }
    s_core.alarms[index].last_fired_date = local_date;
    const alarm_reminder_error_t result = alarm_reminder_core_set_state(
        &s_core,
        alarm_id,
        ALARM_REMINDER_STATE_RINGING
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

esp_err_t alarm_reminder_snooze(
    const char *alarm_id,
    int32_t local_date,
    uint16_t minute_of_day
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t index = alarm_reminder_core_find(&s_core, alarm_id);
    if (index >= s_core.count) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t now_tick = alarm_reminder_core_tick(local_date, minute_of_day);
    if (now_tick <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_core.alarms[index].last_fired_date = local_date;
    const int64_t deadline =
        now_tick + (int64_t)s_core.alarms[index].snooze_minutes;
    const alarm_reminder_error_t result = alarm_reminder_core_set_snooze(
        &s_core,
        alarm_id,
        deadline
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

esp_err_t alarm_reminder_dismiss(const char *alarm_id, int32_t local_date) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t index = alarm_reminder_core_find(&s_core, alarm_id);
    if (index >= s_core.count) {
        return ESP_ERR_INVALID_ARG;
    }
    s_core.alarms[index].last_fired_date = local_date;
    const alarm_reminder_error_t result = alarm_reminder_core_set_state(
        &s_core,
        alarm_id,
        ALARM_REMINDER_STATE_DISMISSED
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

esp_err_t alarm_reminder_mark_missed(
    const char *alarm_id,
    int32_t local_date
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t index = alarm_reminder_core_find(&s_core, alarm_id);
    if (index >= s_core.count) {
        return ESP_ERR_INVALID_ARG;
    }
    s_core.alarms[index].last_fired_date = local_date;
    const alarm_reminder_error_t result = alarm_reminder_core_set_state(
        &s_core,
        alarm_id,
        ALARM_REMINDER_STATE_MISSED
    );
    if (result != ALARM_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_persist();
}

const char *alarm_reminder_state_name(alarm_reminder_state_t state) {
    switch (state) {
        case ALARM_REMINDER_STATE_IDLE:
            return "idle";
        case ALARM_REMINDER_STATE_RINGING:
            return "ringing";
        case ALARM_REMINDER_STATE_SNOOZED:
            return "snoozed";
        case ALARM_REMINDER_STATE_DISMISSED:
            return "dismissed";
        case ALARM_REMINDER_STATE_MISSED:
            return "missed";
        default:
            return "unknown";
    }
}

const char *alarm_reminder_error_name(alarm_reminder_error_t error) {
    switch (error) {
        case ALARM_REMINDER_OK:
            return "ok";
        case ALARM_REMINDER_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case ALARM_REMINDER_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case ALARM_REMINDER_ERR_FULL:
            return "full";
        case ALARM_REMINDER_ERR_NOT_FOUND:
            return "not_found";
        case ALARM_REMINDER_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t alarm_reminder_initialize(void) {
    return alarm_reminder_init();
}

const module_descriptor_t *alarm_reminder_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "alarm_reminder",
        .version = "1.0.0",
        .initialize = alarm_reminder_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
