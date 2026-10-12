#include "english_course.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "esp_heap_caps.h"
#include "english_course_core.h"

// The bounded lesson cache and the practice attempt log persist as one binary
// blob so the device keeps the English catalog and the child's pronunciation
// scores across restarts without a dedicated partition.
#define ENGLISH_COURSE_STORE_KEY "english_course_index"

// The lesson catalog is large enough that a static copy exhausts internal
// DRAM, so the cache is allocated once from PSRAM (internal RAM as a fallback)
// and every access goes through this pointer.
static english_course_core_t *s_core;
static bool s_ready;

static esp_err_t english_course_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        ENGLISH_COURSE_STORE_KEY,
        s_core,
        sizeof(*s_core)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void english_course_restore(void) {
    // Read straight into the allocated cache so a corrupt or oversized blob
    // never needs a second full-size buffer on the stack.
    size_t size = sizeof(*s_core);
    const esp_err_t read = config_store_get_blob(
        ENGLISH_COURSE_STORE_KEY,
        s_core,
        &size
    );
    if (read == ESP_OK && size == sizeof(*s_core) &&
        s_core->lesson_count <= ENGLISH_COURSE_LESSON_CAPACITY &&
        s_core->attempt_count <= ENGLISH_COURSE_ATTEMPT_CAPACITY) {
        return;
    }
    // A missing or malformed blob starts from an empty, valid cache so a
    // corrupt store never blocks startup.
    english_course_core_init(s_core);
}

esp_err_t english_course_init(void) {
    if (s_core == NULL) {
        s_core = (english_course_core_t *)heap_caps_malloc_prefer(
            sizeof(*s_core),
            2,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
        if (s_core == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    english_course_core_init(s_core);
    english_course_restore();
    s_ready = true;
    return ESP_OK;
}

bool english_course_is_ready(void) {
    return s_ready;
}

esp_err_t english_course_upsert_lesson(const english_course_lesson_t *lesson) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const english_course_error_t result = english_course_core_upsert_lesson(
        s_core,
        lesson
    );
    if (result != ENGLISH_COURSE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return english_course_persist();
}

esp_err_t english_course_remove_lesson(const char *lesson_id) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const english_course_error_t result = english_course_core_remove_lesson(
        s_core,
        lesson_id
    );
    if (result != ENGLISH_COURSE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return english_course_persist();
}

esp_err_t english_course_list_lessons(
    english_course_lesson_t *lessons_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lessons_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core->lesson_count < capacity
        ? s_core->lesson_count
        : capacity;
    for (size_t index = 0; index < copied; ++index) {
        lessons_out[index] = s_core->lessons[index];
    }
    *count_out = s_core->lesson_count;
    return ESP_OK;
}

bool english_course_get_lesson(
    const char *lesson_id,
    english_course_lesson_t *lesson_out
) {
    if (lesson_id == NULL || lesson_out == NULL) {
        return false;
    }
    const size_t index = english_course_core_find_lesson(s_core, lesson_id);
    if (index >= s_core->lesson_count) {
        return false;
    }
    *lesson_out = s_core->lessons[index];
    return true;
}

size_t english_course_lesson_count(void) {
    return s_core->lesson_count;
}

esp_err_t english_course_record_attempt(
    const english_course_attempt_t *attempt
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const english_course_error_t result =
        english_course_core_record_attempt(s_core, attempt);
    if (result != ENGLISH_COURSE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return english_course_persist();
}

esp_err_t english_course_list_attempts(
    english_course_attempt_t *attempts_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (attempts_out == NULL && capacity > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t copied = s_core->attempt_count < capacity
        ? s_core->attempt_count
        : capacity;
    for (size_t index = 0; index < copied; ++index) {
        attempts_out[index] = s_core->attempts[index];
    }
    *count_out = s_core->attempt_count;
    return ESP_OK;
}

esp_err_t english_course_lesson_progress(
    const char *lesson_id,
    english_course_item_progress_t *progress_out,
    size_t capacity,
    size_t *count_out
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const english_course_error_t result =
        english_course_core_lesson_progress(
            s_core,
            lesson_id,
            progress_out,
            capacity,
            count_out
        );
    if (result != ENGLISH_COURSE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t english_course_attempt_count(void) {
    return s_core->attempt_count;
}

esp_err_t english_course_clear_attempts(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const english_course_error_t result =
        english_course_core_clear_attempts(s_core);
    if (result != ENGLISH_COURSE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return english_course_persist();
}

const char *english_course_item_type_name(english_course_item_type_t type) {
    switch (type) {
        case ENGLISH_ITEM_WORD:
            return "word";
        case ENGLISH_ITEM_SENTENCE:
            return "sentence";
        default:
            return "unknown";
    }
}

bool english_course_item_type_from_name(
    const char *name,
    english_course_item_type_t *type_out
) {
    if (name == NULL) {
        return false;
    }
    english_course_item_type_t parsed;
    if (strcmp(name, "word") == 0) {
        parsed = ENGLISH_ITEM_WORD;
    } else if (strcmp(name, "sentence") == 0) {
        parsed = ENGLISH_ITEM_SENTENCE;
    } else {
        return false;
    }
    if (type_out != NULL) {
        *type_out = parsed;
    }
    return true;
}

const char *english_course_error_name(english_course_error_t error) {
    switch (error) {
        case ENGLISH_COURSE_OK:
            return "ok";
        case ENGLISH_COURSE_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case ENGLISH_COURSE_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case ENGLISH_COURSE_ERR_FULL:
            return "full";
        case ENGLISH_COURSE_ERR_NOT_FOUND:
            return "not_found";
        case ENGLISH_COURSE_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t english_course_initialize(void) {
    return english_course_init();
}

const module_descriptor_t *english_course_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "english_course",
        .version = "1.0.0",
        .initialize = english_course_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
