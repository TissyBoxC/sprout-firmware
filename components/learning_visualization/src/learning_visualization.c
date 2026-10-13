#include "learning_visualization.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "learning_visualization_sync_core.h"

// One bounded binary blob persists the guardian visualization policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define LEARNING_STORE_KEY "learning_visualization_policy"

static learning_policy_t s_policy;
static learning_visual_state_t s_state;
static uint32_t s_feedback_events;
static uint32_t s_session_minutes;
static char s_lesson_id[LEARNING_LESSON_ID_SIZE];
static bool s_ready;

static esp_err_t learning_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        LEARNING_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return LEARNING_VISUALIZATION_ERR_STORAGE;
    }
    return ESP_OK;
}

static void learning_restore(void) {
    learning_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        LEARNING_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        learning_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = learning_default();
}

static void learning_clear_lesson(void) {
    s_lesson_id[0] = '\0';
}

esp_err_t learning_visualization_init(void) {
    learning_restore();
    // The surface always restarts idle with no linked lesson; the feedback
    // counter is session-local and never persisted.
    s_state = LEARNING_VISUAL_IDLE;
    s_feedback_events = 0u;
    s_session_minutes = 0u;
    learning_clear_lesson();
    s_ready = true;
    return ESP_OK;
}

bool learning_visualization_is_ready(void) {
    return s_ready;
}

esp_err_t learning_visualization_apply(const learning_policy_t *policy) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (policy == NULL || !learning_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    // Revoking the feature mid-session returns the surface to idle so a stale
    // lesson never keeps animating under a disabled policy.
    if (s_policy.enabled == 0u) {
        s_state = LEARNING_VISUAL_IDLE;
        learning_clear_lesson();
    }
    return ESP_OK;
}

esp_err_t learning_visualization_set(const learning_policy_t *policy) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = learning_clamp(policy);
    if (s_policy.enabled == 0u) {
        s_state = LEARNING_VISUAL_IDLE;
        learning_clear_lesson();
    }
    return learning_persist();
}

esp_err_t learning_visualization_apply_remote(
    const learning_visualization_remote_t *remote
) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    learning_policy_t policy = {};
    if (!learning_visualization_sync_core_build_policy(remote, &policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    return learning_visualization_set(&policy);
}

learning_policy_t learning_visualization_get(void) {
    return s_policy;
}

esp_err_t learning_visualization_begin_lesson(const char *lesson_id) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (!learning_can_visualize(&s_policy)) {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    if (s_policy.lesson_linkage_enabled == 0u) {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    if (!learning_lesson_id_valid(lesson_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    // Replacing the lesson id bounds the stored copy to the fixed buffer.
    size_t length = strlen(lesson_id);
    if (length >= LEARNING_LESSON_ID_SIZE) {
        length = LEARNING_LESSON_ID_SIZE - 1u;
    }
    memcpy(s_lesson_id, lesson_id, length);
    s_lesson_id[length] = '\0';
    s_state = LEARNING_VISUAL_LESSON;
    return ESP_OK;
}

esp_err_t learning_visualization_report_feedback(void) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (!learning_can_visualize(&s_policy)) {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    if (s_policy.touch_feedback_enabled == 0u) {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    if (s_feedback_events < LEARNING_MAX_FEEDBACK_EVENTS) {
        s_feedback_events += 1u;
    }
    s_state = LEARNING_VISUAL_FEEDBACK;
    return ESP_OK;
}

esp_err_t learning_visualization_show_reward(void) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (!learning_can_visualize(&s_policy)) {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    if (s_policy.reward_effects_enabled == 0u) {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    if (s_feedback_events < LEARNING_MAX_FEEDBACK_EVENTS) {
        s_feedback_events += 1u;
    }
    s_state = LEARNING_VISUAL_REWARD;
    return ESP_OK;
}

esp_err_t learning_visualization_pause(void) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (s_state != LEARNING_VISUAL_LESSON &&
        s_state != LEARNING_VISUAL_FEEDBACK &&
        s_state != LEARNING_VISUAL_REWARD) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    s_state = LEARNING_VISUAL_PAUSED;
    return ESP_OK;
}

esp_err_t learning_visualization_resume(void) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (s_state != LEARNING_VISUAL_PAUSED) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    if (!learning_can_visualize(&s_policy) ||
        s_lesson_id[0] == '\0') {
        return LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED;
    }
    s_state = LEARNING_VISUAL_LESSON;
    return ESP_OK;
}

esp_err_t learning_visualization_stop(void) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    s_state = LEARNING_VISUAL_IDLE;
    s_session_minutes = 0u;
    learning_clear_lesson();
    return ESP_OK;
}

esp_err_t learning_visualization_report_error(void) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    s_state = LEARNING_VISUAL_ERROR;
    return ESP_OK;
}

learning_visual_state_t learning_visualization_visual_state(void) {
    return s_state;
}

learning_animation_level_t learning_visualization_animation_level(void) {
    return learning_effective_animation(&s_policy, s_state);
}

const char *learning_visualization_linked_lesson(void) {
    return s_lesson_id;
}

uint32_t learning_visualization_feedback_events(void) {
    return s_feedback_events;
}

esp_err_t learning_visualization_report_session_minutes(
    uint32_t session_minutes
) {
    if (!s_ready) {
        return LEARNING_VISUALIZATION_ERR_INVALID_STATE;
    }
    s_session_minutes = session_minutes > LEARNING_MAX_SESSION_MINUTES_REPORT
        ? LEARNING_MAX_SESSION_MINUTES_REPORT
        : session_minutes;
    return ESP_OK;
}

learning_visualization_status_t learning_visualization_status(void) {
    learning_visualization_status_t status = {
        .supported = true,
        .touch_supported = s_policy.touch_feedback_enabled != 0u,
        .linked_lesson_id = {0},
        .visual_state = s_state,
        .feedback_style = (learning_style_t)s_policy.feedback_style,
        .animation_level =
            learning_effective_animation(&s_policy, s_state),
        .feedback_events = s_feedback_events,
        .session_minutes = s_session_minutes,
        .revision = s_policy.revision,
    };
    // The linked lesson id is copied defensively; an idle surface reports none.
    if (s_state != LEARNING_VISUAL_IDLE && s_lesson_id[0] != '\0') {
        size_t length = strlen(s_lesson_id);
        if (length >= sizeof(status.linked_lesson_id)) {
            length = sizeof(status.linked_lesson_id) - 1u;
        }
        memcpy(status.linked_lesson_id, s_lesson_id, length);
        status.linked_lesson_id[length] = '\0';
    } else {
        status.linked_lesson_id[0] = '\0';
    }
    // A disabled policy reports a fully idle, animation-off surface so a stale
    // state never reads as an active animation.
    if (s_policy.enabled == 0u) {
        status.visual_state = LEARNING_VISUAL_IDLE;
        status.animation_level = LEARNING_ANIMATION_OFF;
        status.linked_lesson_id[0] = '\0';
        status.feedback_events = 0u;
        status.session_minutes = 0u;
    }
    return status;
}

static esp_err_t learning_visualization_initialize(void) {
    return learning_visualization_init();
}

const module_descriptor_t *learning_visualization_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "learning_visualization",
        .version = "1.0.0",
        .initialize = learning_visualization_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
