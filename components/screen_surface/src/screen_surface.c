#include "screen_surface.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"

// One bounded binary blob persists the screen policy without a dedicated
// partition; a schema change is detected and replaced with the conservative
// default instead of applied.
#define SCREEN_STORE_KEY "screen_policy"

static screen_policy_t s_policy;
static screen_expression_t s_expression;
static screen_animation_t s_animation;
static uint32_t s_animation_elapsed_ms;
static uint32_t s_animation_frame;
static screen_video_state_t s_video_state;
static screen_point_read_entry_t s_point_read[SCREEN_POINT_READ_CAPACITY];
static size_t s_point_read_count;
static bool s_ready;

// Normalize an entry's term in place so a stored term and a looked-up term are
// compared on the same trimmed form. Returns false when the term is empty.
static bool normalize_entry(screen_point_read_entry_t *entry) {
    size_t start = 0u;
    while (entry->term[start] == ' ') {
        start++;
    }
    size_t end = start;
    while (entry->term[end] != '\0') {
        end++;
    }
    while (end > start && entry->term[end - 1u] == ' ') {
        end--;
    }
    if (end == start) {
        entry->term[0] = '\0';
        return false;
    }
    const size_t length = end - start;
    for (size_t index = 0u; index < length; index++) {
        entry->term[index] = entry->term[start + index];
    }
    entry->term[length] = '\0';
    return true;
}

static esp_err_t screen_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        SCREEN_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void screen_restore(void) {
    screen_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        SCREEN_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        screen_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = screen_default();
}

esp_err_t screen_surface_init(void) {
    screen_restore();
    s_expression = (screen_expression_t)s_policy.default_expression;
    s_animation = SCREEN_ANIMATION_NONE;
    s_animation_elapsed_ms = 0u;
    s_animation_frame = 0u;
    s_video_state = SCREEN_VIDEO_IDLE;
    s_point_read_count = 0u;
    memset(s_point_read, 0, sizeof(s_point_read));
    s_ready = true;
    return ESP_OK;
}

bool screen_surface_is_ready(void) {
    return s_ready;
}

esp_err_t screen_surface_apply(const screen_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !screen_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t screen_surface_set(const screen_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = screen_clamp(policy);
    // A disabled surface falls back to the policy default so a stale
    // expression never lingers on screen.
    if (s_policy.enabled == 0u) {
        s_expression = (screen_expression_t)s_policy.default_expression;
        s_animation = SCREEN_ANIMATION_NONE;
    }
    return screen_persist();
}

screen_policy_t screen_surface_get(void) {
    return s_policy;
}

esp_err_t screen_surface_show_expression(screen_expression_t expression) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!screen_expression_valid((uint32_t)expression)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_expression = s_policy.enabled != 0u
        ? expression
        : (screen_expression_t)s_policy.default_expression;
    return ESP_OK;
}

screen_expression_t screen_surface_current_expression(void) {
    return s_expression;
}

esp_err_t screen_surface_play_animation(screen_animation_t animation) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!screen_animation_valid((uint32_t)animation)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_policy.enabled == 0u || s_policy.animation_enabled == 0u) {
        s_animation = SCREEN_ANIMATION_NONE;
        s_animation_elapsed_ms = 0u;
        s_animation_frame = 0u;
        return ESP_OK;
    }
    s_animation = animation;
    s_animation_elapsed_ms = 0u;
    s_animation_frame = 0u;
    return ESP_OK;
}

screen_animation_t screen_surface_current_animation(void) {
    return s_animation;
}

uint32_t screen_surface_advance_animation(uint32_t elapsed_ms) {
    if (!s_ready || s_animation == SCREEN_ANIMATION_NONE) {
        return s_animation_frame;
    }
    // Widen before accumulating so a large elapsed window never wraps the
    // frame clock; the frame interval is always at least one millisecond.
    s_animation_elapsed_ms += elapsed_ms;
    while (screen_animation_frame_due(&s_policy, s_animation_elapsed_ms)) {
        s_animation_elapsed_ms -= s_policy.animation_frame_ms;
        s_animation_frame++;
    }
    return s_animation_frame;
}

esp_err_t screen_surface_point_read_put(
    const screen_point_read_entry_t *entry
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (entry == NULL || entry->term[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    screen_point_read_entry_t normalized = *entry;
    if (!normalize_entry(&normalized)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0u; index < s_point_read_count; index++) {
        if (strcmp(s_point_read[index].term, normalized.term) == 0) {
            s_point_read[index] = normalized;
            return ESP_OK;
        }
    }
    if (s_point_read_count >= SCREEN_POINT_READ_CAPACITY) {
        return ESP_ERR_INVALID_STATE;
    }
    s_point_read[s_point_read_count] = normalized;
    s_point_read_count++;
    return ESP_OK;
}

esp_err_t screen_surface_point_read_lookup(
    const char *term,
    screen_point_read_entry_t *output
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_policy.point_read_enabled == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0u; index < s_point_read_count; index++) {
        if (screen_point_read_matches(&s_point_read[index], term)) {
            *output = s_point_read[index];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

size_t screen_surface_point_read_count(void) {
    return s_point_read_count;
}

void screen_surface_point_read_clear(void) {
    s_point_read_count = 0u;
    memset(s_point_read, 0, sizeof(s_point_read));
}

esp_err_t screen_surface_video_begin(
    uint32_t requested_bitrate_kbps,
    uint32_t requested_fps,
    screen_video_plan_t *output
) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_policy.enabled == 0u || s_policy.video_enabled == 0u) {
        s_video_state = SCREEN_VIDEO_IDLE;
        return ESP_ERR_INVALID_STATE;
    }
    *output = screen_video_plan(
        &s_policy,
        requested_bitrate_kbps,
        requested_fps
    );
    s_video_state = SCREEN_VIDEO_LOADING;
    return ESP_OK;
}

esp_err_t screen_surface_video_pause(void) {
    if (s_video_state != SCREEN_VIDEO_PLAYING &&
        s_video_state != SCREEN_VIDEO_LOADING) {
        return ESP_ERR_INVALID_STATE;
    }
    s_video_state = SCREEN_VIDEO_PAUSED;
    return ESP_OK;
}

esp_err_t screen_surface_video_resume(void) {
    if (s_video_state != SCREEN_VIDEO_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_video_state = SCREEN_VIDEO_PLAYING;
    return ESP_OK;
}

esp_err_t screen_surface_video_stop(void) {
    s_video_state = SCREEN_VIDEO_IDLE;
    return ESP_OK;
}

screen_video_state_t screen_surface_video_state(void) {
    return s_video_state;
}

static esp_err_t screen_surface_initialize(void) {
    return screen_surface_init();
}

const module_descriptor_t *screen_surface_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "screen_surface",
        .version = "1.0.0",
        .initialize = screen_surface_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
