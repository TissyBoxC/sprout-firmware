#include "local_command.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "local_command_core.h"

// The bounded local command cache persists as one binary blob so a device can
// keep answering the same trigger phrases across restarts while offline.
#define LOCAL_COMMAND_STORE_KEY "local_command_index"

static local_command_core_t s_core;
static bool s_ready;

static esp_err_t local_command_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        LOCAL_COMMAND_STORE_KEY,
        &s_core,
        sizeof(s_core)
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
    const local_command_error_t result = local_command_core_replace_all(
        &s_core,
        commands,
        command_count
    );
    switch (result) {
        case LOCAL_COMMAND_OK:
            return local_command_persist();
        case LOCAL_COMMAND_ERR_FULL:
            return ESP_ERR_INVALID_SIZE;
        default:
            return ESP_ERR_INVALID_ARG;
    }
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
    const size_t copied = s_core.count < capacity ? s_core.count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        commands_out[index] = s_core.commands[index];
    }
    *count_out = s_core.count;
    return ESP_OK;
}

bool local_command_resolve(
    const char *phrase,
    local_command_t *command_out
) {
    if (!s_ready) {
        return false;
    }
    return local_command_core_resolve(&s_core, phrase, command_out);
}

size_t local_command_count(void) {
    return s_core.count;
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
