#include "care_view.h"

#include <cstring>

#include "ArduinoJson.h"

#include "audio_output.h"
#include "camera_capture.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "prompt_tone.h"
#include "screen_surface.h"

namespace {

constexpr const char *kTag = "care_view";

// One status snapshot is kept for diagnostics; the state machine owns the truth.
care_view_status_t g_status = {};

// A start command older than this is treated as stale rather than acted on. The
// command channel is durable, so a device that was offline can receive a start
// long after the guardian gave up.
constexpr uint64_t kStaleCommandWindowMs = 60u * 1000u;

uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void refresh_status() {
    care_view_snapshot_t snapshot = care_view_core_snapshot();
    g_status.phase = snapshot.phase;
    g_status.capture_allowed = care_view_core_allows_capture();
    g_status.frames_sent = snapshot.frames_sent;
    g_status.frames_dropped = snapshot.frames_dropped;
    std::strncpy(g_status.session_id, snapshot.session_id,
                 sizeof(g_status.session_id) - 1);
    g_status.session_id[sizeof(g_status.session_id) - 1] = '\0';

    uint64_t current = now_ms();
    if (snapshot.deadline_ms > current) {
        g_status.remaining_seconds =
            static_cast<uint32_t>((snapshot.deadline_ms - current) / 1000u);
    } else {
        g_status.remaining_seconds = 0;
    }
}

/**
 * Report whether the device can prove to the child that it is being watched.
 *
 * Both the screen and the speaker must be available: an on-screen notice alone
 * cannot reassure a child who is not looking at the device, and a tone alone
 * says nothing about why. If either is missing the view is refused.
 */
bool indication_available() {
    if (!camera_capture_is_ready()) {
        return false;
    }
    // screen_surface resets its state on every init, so readiness is queried
    // instead of re-initialized: doing so here would clear a notice that is
    // already on screen.
    if (!screen_surface_is_ready()) {
        return false;
    }
    return audio_output_is_ready();
}

void announce_view_started() {
    // Audible notice first: the child hears it even while looking away. The
    // capture-started tone is the existing prompt that already means "the
    // device is now listening or watching", so no new sound is invented.
    (void)prompt_tone_play(PROMPT_TONE_CAPTURE_STARTED);
}

void release_capture() {
    if (camera_capture_is_ready()) {
        (void)camera_capture_abort();
    }
}

void report_acknowledgement(const char *result_code, const char *session_id) {
    // Logs carry only the stable reason and a bounded identifier; no frame
    // content, no URL, and no token ever reaches the log.
    ESP_LOGI(kTag, "session=%s result=%s", session_id, result_code);
}

esp_err_t handle_start(JsonObjectConst payload) {
    const char *session_id = payload["session_id"];
    if (session_id == nullptr || session_id[0] == '\0') {
        ++g_status.commands_rejected;
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t deadline_ms = 0;
    const char *expires_at = payload["expires_at"];
    if (expires_at != nullptr) {
        // The deadline is advisory: device policy clamps it in the state
        // machine, so a wrong or missing value cannot extend a view.
        int seconds = 0;
        if (std::sscanf(expires_at, "%*[^T]T%*d:%*d:%d", &seconds) == 1) {
            deadline_ms = now_ms() + static_cast<uint64_t>(seconds) * 1000u;
        }
    }
    if (deadline_ms == 0) {
        deadline_ms = now_ms() + static_cast<uint64_t>(CONFIG_CARE_VIEW_MAX_SESSION_SECONDS) * 1000u;
    }

    bool can_show = indication_available();
    care_view_error_t error = care_view_core_start(
        session_id, now_ms(), deadline_ms, CONFIG_CARE_VIEW_MAX_SESSION_SECONDS, can_show);

    if (error == CARE_VIEW_ERR_BUSY) {
        ++g_status.commands_rejected;
        report_acknowledgement("busy", session_id);
        return ESP_ERR_INVALID_STATE;
    }
    if (error != CARE_VIEW_OK) {
        ++g_status.sessions_refused;
        ++g_status.camera_failures;
        report_acknowledgement("camera_unavailable", session_id);
        return ESP_ERR_INVALID_STATE;
    }

    announce_view_started();
    // Only after the prompt was issued does the machine allow capture, and only
    // after this call can a frame leave the device.
    care_view_error_t begin = care_view_core_begin_viewing(now_ms());
    if (begin != CARE_VIEW_OK) {
        care_view_core_end(now_ms(), CARE_VIEW_END_EXPIRED);
        ++g_status.expired;
        report_acknowledgement("expired", session_id);
        return ESP_ERR_INVALID_STATE;
    }

    ++g_status.sessions_started;
    report_acknowledgement("viewing_started", session_id);
    return ESP_OK;
}

esp_err_t handle_stop(JsonObjectConst payload) {
    const char *session_id = payload["session_id"];
    care_view_snapshot_t snapshot = care_view_core_snapshot();
    if (snapshot.phase == CARE_VIEW_PHASE_IDLE) {
        return ESP_OK;
    }
    // A stop for a different session must not end the one that is running.
    if (session_id != nullptr && session_id[0] != '\0' &&
        std::strcmp(session_id, snapshot.session_id) != 0) {
        return ESP_OK;
    }
    care_view_core_end(now_ms(), CARE_VIEW_END_GUARDIAN_CLOSED);
    release_capture();
    report_acknowledgement("guardian_closed", snapshot.session_id);
    refresh_status();
    return ESP_OK;
}

}  // namespace

esp_err_t care_view_bind_command_channel(void) {
    // The shared dispatcher calls care_view_handle_command for this module.
    if (!camera_capture_is_ready()) {
        ESP_LOGW(kTag, "camera unavailable, live view disabled");
    }
    care_view_core_init();
    refresh_status();
    return ESP_OK;
}

esp_err_t care_view_handle_command(const char *payload, size_t length) {
    if (payload == nullptr || length == 0) {
        ++g_status.commands_rejected;
        return ESP_ERR_INVALID_ARG;
    }
    // A bounded buffer keeps a hostile payload from exhausting memory.
    if (length > 2048) {
        ++g_status.commands_rejected;
        return ESP_ERR_INVALID_ARG;
    }

    JsonDocument document;
    DeserializationError error = deserializeJson(document, payload, length);
    if (error) {
        ++g_status.commands_rejected;
        return ESP_ERR_INVALID_ARG;
    }
    JsonObjectConst root = document.as<JsonObjectConst>();
    const char *command_type = root["command_type"];
    if (command_type == nullptr) {
        return ESP_OK;  // Not addressed to this module.
    }

    esp_err_t result = ESP_OK;
    if (std::strcmp(command_type, "care_view_start") == 0) {
        result = handle_start(root["payload"].as<JsonObjectConst>());
    } else if (std::strcmp(command_type, "care_view_stop") == 0) {
        result = handle_stop(root["payload"].as<JsonObjectConst>());
    }
    refresh_status();
    return result;
}

void care_view_poll(void) {
    if (care_view_core_is(CARE_VIEW_PHASE_IDLE)) {
        return;
    }
    care_view_end_reason_t reason = CARE_VIEW_END_GUARDIAN_CLOSED;
    if (care_view_core_tick(now_ms(), &reason)) {
        release_capture();
        if (reason == CARE_VIEW_END_EXPIRED) {
            ++g_status.expired;
        }
        report_acknowledgement(care_view_end_reason_name(reason),
                              g_status.session_id);
        refresh_status();
        return;
    }

    // The capture itself runs in the camera pipeline; this poll only keeps the
    // device honest about the deadline and about its own capture permission.
    if (!care_view_core_allows_capture()) {
        return;
    }
    if (!camera_capture_is_ready()) {
        care_view_core_end(now_ms(), CARE_VIEW_END_CAMERA_UNAVAILABLE);
        ++g_status.camera_failures;
        report_acknowledgement("camera_unavailable", g_status.session_id);
        refresh_status();
    }
}

care_view_status_t care_view_status(void) {
    refresh_status();
    return g_status;
}

bool care_view_handle_button(button_input_gesture_t gesture) {
    (void)gesture;
    if (care_view_core_is(CARE_VIEW_PHASE_IDLE)) {
        return false;
    }
    // Any press stops the view. The child must be able to end being watched
    // without knowing which gesture the manufacturer chose.
    care_view_snapshot_t snapshot = care_view_core_snapshot();
    care_view_core_end(now_ms(), CARE_VIEW_END_CHILD_CLOSED);
    release_capture();
    ++g_status.child_closed;
    report_acknowledgement("local_closed", snapshot.session_id);
    refresh_status();
    // Returning false keeps the gesture available for its normal meaning.
    return false;
}

void care_view_shutdown(void) {
    // A module reload must never leave the camera running.
    if (!care_view_core_is(CARE_VIEW_PHASE_IDLE)) {
        care_view_core_end(now_ms(), CARE_VIEW_END_GUARDIAN_CLOSED);
        release_capture();
        refresh_status();
    }
}

const module_descriptor_t *care_view_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "care_view",
        .version = "0.1.0",
        .initialize = care_view_bind_command_channel,
        .shutdown = care_view_shutdown,
    };
    return &descriptor;
}
