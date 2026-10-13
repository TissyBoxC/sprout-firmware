#include "habit_reminder.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "habit_reminder_core.h"

// The bounded reminder store persists as one binary blob so the device keeps
// the guardian's schedule across restarts without a dedicated partition.
#define HABIT_REMINDER_STORE_KEY "habit_index"
CONFIG_STORE_KEY_ASSERT(HABIT_REMINDER_STORE_KEY);

static habit_reminder_core_t s_core;
static bool s_ready;

static esp_err_t habit_reminder_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        HABIT_REMINDER_STORE_KEY,
        &s_core,
        sizeof(s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void habit_reminder_restore(void) {
    habit_reminder_core_t restored = {0};
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        HABIT_REMINDER_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= HABIT_REMINDER_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid store so a
    // corrupt store never blocks startup.
    habit_reminder_core_init(&s_core);
}

esp_err_t habit_reminder_init(void) {
    habit_reminder_core_init(&s_core);
    habit_reminder_restore();
    s_ready = true;
    return ESP_OK;
}

bool habit_reminder_is_ready(void) {
    return s_ready;
}

esp_err_t habit_reminder_upsert(const habit_reminder_t *reminder) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const habit_reminder_error_t result = habit_reminder_core_upsert(
        &s_core,
        reminder
    );
    if (result != HABIT_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return habit_reminder_persist();
}

esp_err_t habit_reminder_remove(const char *reminder_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const habit_reminder_error_t result = habit_reminder_core_remove(
        &s_core,
        reminder_id
    );
    if (result != HABIT_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return habit_reminder_persist();
}

esp_err_t habit_reminder_remove_child(
    const char *child_id,
    size_t *removed_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const habit_reminder_error_t result = habit_reminder_core_remove_child(
        &s_core,
        child_id,
        removed_out
    );
    if (result != HABIT_REMINDER_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return habit_reminder_persist();
}

esp_err_t habit_reminder_list(
    habit_reminder_t *reminders_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (reminders_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        reminders_out[index] = s_core.reminders[index];
    }
    *count_out = s_core.count;
    return ESP_OK;
}

bool habit_reminder_get(
    const char *reminder_id,
    habit_reminder_t *reminder_out
) {
    if (reminder_id == NULL || reminder_out == NULL) {
        return false;
    }
    const size_t index = habit_reminder_core_find(&s_core, reminder_id);
    if (index >= s_core.count) {
        return false;
    }
    *reminder_out = s_core.reminders[index];
    return true;
}

size_t habit_reminder_count(void) {
    return s_core.count;
}

size_t habit_reminder_due_at(
    int weekday,
    uint16_t minute_of_day,
    char (*due_ids_out)[HABIT_REMINDER_ID_SIZE],
    size_t capacity
) {
    return habit_reminder_core_due_at(
        &s_core,
        weekday,
        minute_of_day,
        due_ids_out,
        capacity
    );
}

const char *habit_reminder_error_name(habit_reminder_error_t error) {
    switch (error) {
        case HABIT_REMINDER_OK:
            return "ok";
        case HABIT_REMINDER_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case HABIT_REMINDER_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case HABIT_REMINDER_ERR_FULL:
            return "full";
        case HABIT_REMINDER_ERR_NOT_FOUND:
            return "not_found";
        case HABIT_REMINDER_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t habit_reminder_initialize(void) {
    return habit_reminder_init();
}

const module_descriptor_t *habit_reminder_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "habit_reminder",
        .version = "1.0.0",
        .initialize = habit_reminder_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
