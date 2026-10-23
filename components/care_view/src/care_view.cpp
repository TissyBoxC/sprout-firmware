#include "care_view.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ArduinoJson.h"

#include "audio_output.h"
#include "camera_capture.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
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

// Credential keys shared with the rest of the device: the provisioning step
// writes one mutual-TLS identity and every component that talks to the broker
// reads the same copy rather than provisioning its own.
constexpr const char *kMqttCaKey = "mqtt_ca";
constexpr const char *kMqttCertKey = "mqtt_cert";
constexpr size_t kMqttPemSize = 4096;
constexpr size_t kAckTopicSize = 128;
constexpr size_t kAckPayloadSize = 256;

esp_mqtt_client_handle_t g_mqtt_client = nullptr;
char g_ack_topic[kAckTopicSize] = {};
char *g_mqtt_ca_pem = nullptr;
char *g_mqtt_cert_pem = nullptr;

constexpr size_t kSessionUrlSize = 256;
constexpr size_t kDeclinePayloadSize = 96;
constexpr size_t kBaseUrlSize = 128;
constexpr int kSessionRequestTimeoutMs = 8000;

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

// Tell the platform that this device accepted, refused, or closed a session.
//
// The acknowledgement above only reports what the device decided; the platform
// keeps the session in "requested" until this call moves it, and the guardian's
// stream is authorized off that stored state. A device that publishes an ack but
// never calls this leaves a session the guardian can neither watch nor close.
//
// A failure here is not retried in a loop: the session still expires on its
// deadline, and the state machine must not stall waiting for the network.
void report_session_state(
    const char *path_suffix,
    const char *session_id,
    const char *reason
) {
    if (session_id == nullptr || session_id[0] == '\0') {
        return;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {};
    if (device_identity_copy(device_id, sizeof(device_id)) != ESP_OK ||
        device_id[0] == '\0') {
        return;
    }
    char base_url[kBaseUrlSize] = {};
    if (device_binding_client_get_platform_base_url(
            base_url,
            sizeof(base_url)
        ) != ESP_OK ||
        base_url[0] == '\0') {
        return;
    }
    char token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {};
    if (device_binding_client_copy_session_token(token, sizeof(token)) != ESP_OK ||
        token[0] == '\0') {
        // Without a session the platform answers 401, and the session expires on
        // its own deadline. No request is worth sending.
        memset(base_url, 0, sizeof(base_url));
        return;
    }

    char url[kSessionUrlSize] = {};
    snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/care-view-sessions/%s%s",
        base_url,
        device_id,
        session_id,
        path_suffix
    );

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kSessionRequestTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        memset(url, 0, sizeof(url));
        memset(token, 0, sizeof(token));
        memset(base_url, 0, sizeof(base_url));
        return;
    }
    char authorization[DEVICE_BINDING_SESSION_TOKEN_SIZE + 16] = {};
    snprintf(authorization, sizeof(authorization), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // The decline route reads a reason and the other two ignore the body, so the
    // body is only sent where the platform validates it.
    if (reason != nullptr && reason[0] != '\0') {
        char payload[kDeclinePayloadSize] = {};
        const int length = snprintf(
            payload,
            sizeof(payload),
            "{\"reason\":\"%s\"}",
            reason
        );
        if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
            esp_http_client_set_post_field(client, payload, length);
        }
        (void)esp_http_client_perform(client);
        memset(payload, 0, sizeof(payload));
    } else {
        (void)esp_http_client_perform(client);
    }
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    // Only a stable outcome is logged: the session id and the reason already
    // reach the platform, and the log must not become a second copy of them.
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(kTag, "session state report failed status=%d", status_code);
    }
    memset(authorization, 0, sizeof(authorization));
    memset(token, 0, sizeof(token));
    memset(base_url, 0, sizeof(base_url));
    memset(url, 0, sizeof(url));
}

// Publish the session outcome on the device ack topic.
//
// The platform does not infer a live view's progress from polling: a session
// stays "requested" until the device acknowledges it, so an unpublished result
// leaves the guardian staring at a spinner and the device holding a slot. The
// payload carries only a stable reason and the bounded session identifier;
// frames never pass through here.
void report_acknowledgement(const char *result_code, const char *session_id) {
    // Logs carry only the stable reason and a bounded identifier; no frame
    // content, no URL, and no token ever reaches the log.
    ESP_LOGI(kTag, "session=%s result=%s", session_id, result_code);
    if (g_mqtt_client == nullptr || g_ack_topic[0] == '\0') {
        return;
    }
    if (result_code == nullptr) {
        result_code = "ok";
    }
    char payload[kAckPayloadSize] = {};
    const int length = snprintf(
        payload,
        sizeof(payload),
        "{\"session_id\":\"%s\",\"result_code\":\"%s\"}",
        session_id == nullptr ? "" : session_id,
        result_code
    );
    // A truncated envelope would describe a different outcome, so it is dropped
    // rather than sent.
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        (void)esp_mqtt_client_publish(
            g_mqtt_client,
            g_ack_topic,
            payload,
            length,
            1,
            0
        );
    }
    memset(payload, 0, sizeof(payload));
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
        report_session_state("/decline", session_id, "camera_unavailable");
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
    // The guardian's stream is authorized from the platform's stored state, and
    // only this call moves the session out of "requested".
    report_session_state("/accept", session_id, nullptr);
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

// Bring up the ack channel. Credentials are optional at this layer: a device in
// factory flow has none yet, and the module must still initialise so the rest of
// the runtime boots. Without a channel the state machine keeps working locally;
// only the platform stops hearing about it, which is why the failure is logged
// once rather than retried in a loop.
static esp_err_t care_view_start_ack_channel(void) {
    char device_id[DEVICE_IDENTIFIER_SIZE] = {};
    if (device_identity_copy(device_id, sizeof(device_id)) != ESP_OK ||
        device_id[0] == '\0') {
        ESP_LOGW(kTag, "device identity unavailable, live view results not reported");
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(
        g_ack_topic,
        sizeof(g_ack_topic),
        "sprout/v1/devices/%s/commands/ack",
        device_id
    );

    g_mqtt_ca_pem = static_cast<char *>(calloc(1, kMqttPemSize));
    g_mqtt_cert_pem = static_cast<char *>(calloc(1, kMqttPemSize));
    if (g_mqtt_ca_pem == nullptr || g_mqtt_cert_pem == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (config_store_get_string(kMqttCaKey, g_mqtt_ca_pem, kMqttPemSize) != ESP_OK ||
        config_store_get_string(kMqttCertKey, g_mqtt_cert_pem, kMqttPemSize) != ESP_OK) {
        ESP_LOGW(kTag, "broker credentials unavailable, live view results not reported");
        return ESP_ERR_NOT_FOUND;
    }

    esp_mqtt_client_config_t config = {};
    config.broker.verification.certificate = g_mqtt_ca_pem;
    config.credentials.authentication.certificate = g_mqtt_cert_pem;
    config.credentials.client_id = device_id;
    g_mqtt_client = esp_mqtt_client_init(&config);
    if (g_mqtt_client == nullptr) {
        return ESP_FAIL;
    }
    return esp_mqtt_client_start(g_mqtt_client);
}

esp_err_t care_view_bind_command_channel(void) {
    // Command dispatch belongs to device_runtime_reporter, which forwards the
    // raw payload to care_view_handle_command. This hook only initialises the
    // local state machine and the ack channel.
    if (!camera_capture_is_ready()) {
        ESP_LOGW(kTag, "camera unavailable, live view disabled");
    }
    care_view_core_init();
    // A failure here is not fatal: the view still runs and still shows the
    // child the notice, so the guardian's session closes on its deadline.
    (void)care_view_start_ack_channel();
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
        // The platform owns the session record, so a view that stops locally
        // without this call leaves the guardian's screen showing a stream that
        // no longer exists.
        report_session_state(
            "/end",
            g_status.session_id,
            care_view_end_reason_name(reason)
        );
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
        report_session_state(
            "/end",
            g_status.session_id,
            "camera_unavailable"
        );
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
    report_session_state("/end", snapshot.session_id, "child_closed");
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
