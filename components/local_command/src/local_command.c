#include "local_command.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "local_command_core.h"
#include "local_command_sync_core.h"

// The bounded local command cache persists as one binary blob so a device can
// keep answering the same trigger phrases across restarts while offline.
#define LOCAL_COMMAND_STORE_KEY "local_command_index"

static local_command_core_t s_core;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static esp_err_t local_command_persist_core(
    const local_command_core_t *core
) {
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t stored = config_store_set_blob(
        LOCAL_COMMAND_STORE_KEY,
        core,
        sizeof(*core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void local_command_restore(void) {
    local_command_core_t restored;
    memset(&restored, 0, sizeof(restored));
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        LOCAL_COMMAND_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.count <= LOCAL_COMMAND_CAPACITY) {
        s_core = restored;
        return;
    }
    // A missing or malformed blob starts from an empty, valid cache so a
    // corrupt store never blocks startup.
    local_command_core_init(&s_core);
}

esp_err_t local_command_init(void) {
    if (s_ready) {
        return ESP_OK;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    local_command_core_init(&s_core);
    local_command_restore();
    s_ready = true;
    return ESP_OK;
}

bool local_command_is_ready(void) {
    return s_ready;
}

esp_err_t local_command_replace_all(
    const local_command_t *commands,
    size_t command_count
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    local_command_core_t updated;
    const local_command_error_t result = local_command_core_replace_all(
        &updated,
        commands,
        command_count
    );
    switch (result) {
        case LOCAL_COMMAND_OK:
            break;
        case LOCAL_COMMAND_ERR_FULL:
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_SIZE;
        default:
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = local_command_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    xSemaphoreGive(s_lock);
    return persisted;
}

esp_err_t local_command_replace_remote(
    const local_command_t *commands,
    size_t command_count
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    local_command_core_t updated;
    const local_command_error_t result = local_command_sync_core_replace(
        &updated,
        commands,
        command_count
    );
    if (result != LOCAL_COMMAND_OK) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t persisted = local_command_persist_core(&updated);
    if (persisted == ESP_OK) {
        s_core = updated;
    }
    xSemaphoreGive(s_lock);
    return persisted;
}

esp_err_t local_command_list(
    local_command_t *commands_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (commands_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        commands_out[index] = s_core.commands[index];
    }
    *count_out = s_core.count;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool local_command_resolve(
    const char *phrase,
    local_command_t *command_out
) {
    if (!s_ready) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool resolved = local_command_core_resolve(
        &s_core,
        phrase,
        command_out
    );
    xSemaphoreGive(s_lock);
    return resolved;
}

size_t local_command_count(void) {
    if (!s_ready) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t count = s_core.count;
    xSemaphoreGive(s_lock);
    return count;
}

const char *local_command_action_name(local_command_action_t action) {
    switch (action) {
        case LOCAL_COMMAND_ACTION_PLAY_PACKAGE:
            return "play_package";
        case LOCAL_COMMAND_ACTION_PAUSE:
            return "pause";
        case LOCAL_COMMAND_ACTION_RESUME:
            return "resume";
        case LOCAL_COMMAND_ACTION_STOP:
            return "stop";
        case LOCAL_COMMAND_ACTION_SET_VOLUME:
            return "set_volume";
        case LOCAL_COMMAND_ACTION_SLEEP:
            return "sleep";
        default:
            return "unknown";
    }
}

bool local_command_action_from_name(
    const char *name,
    local_command_action_t *action_out
) {
    if (name == NULL) {
        return false;
    }
    local_command_action_t parsed;
    if (strcmp(name, "play_package") == 0) {
        parsed = LOCAL_COMMAND_ACTION_PLAY_PACKAGE;
    } else if (strcmp(name, "pause") == 0) {
        parsed = LOCAL_COMMAND_ACTION_PAUSE;
    } else if (strcmp(name, "resume") == 0) {
        parsed = LOCAL_COMMAND_ACTION_RESUME;
    } else if (strcmp(name, "stop") == 0) {
        parsed = LOCAL_COMMAND_ACTION_STOP;
    } else if (strcmp(name, "set_volume") == 0) {
        parsed = LOCAL_COMMAND_ACTION_SET_VOLUME;
    } else if (strcmp(name, "sleep") == 0) {
        parsed = LOCAL_COMMAND_ACTION_SLEEP;
    } else {
        return false;
    }
    if (action_out != NULL) {
        *action_out = parsed;
    }
    return true;
}

const char *local_command_error_name(local_command_error_t error) {
    switch (error) {
        case LOCAL_COMMAND_OK:
            return "ok";
        case LOCAL_COMMAND_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case LOCAL_COMMAND_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case LOCAL_COMMAND_ERR_FULL:
            return "full";
        case LOCAL_COMMAND_ERR_NOT_FOUND:
            return "not_found";
        case LOCAL_COMMAND_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t local_command_initialize(void) {
    return local_command_init();
}

const module_descriptor_t *local_command_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "local_command",
        .version = "1.0.0",
        .initialize = local_command_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
