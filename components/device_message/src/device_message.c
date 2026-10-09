#include "device_message.h"

#include <stddef.h>
#include <string.h>

#include "esp_err.h"

#include "device_capabilities.h"
#include "device_message_core.h"

#if CONFIG_FEATURE_LED_INDICATOR && __has_include("led_indicator.h")
#include "led_indicator.h"
#include "esp_log.h"
#define DEVICE_MESSAGE_HAS_LED 1
#else
#define DEVICE_MESSAGE_HAS_LED 0
#endif

#if DEVICE_MESSAGE_HAS_LED
static const char *const TAG = "device_message";
#endif

static device_message_t s_last_message;
static device_message_snapshot_t s_snapshot;
static bool s_ready;
static bool s_has_message;

static bool has_display(void) {
    return device_capabilities_has(
        device_capabilities_get(),
        DEVICE_CAPABILITY_DISPLAY
    );
}

static void set_error(
    device_message_error_t *error_out,
    device_message_error_t value
) {
    if (error_out != NULL) {
        *error_out = value;
    }
}

static void bounded_copy(char *destination, size_t size, const char *source) {
    if (destination == NULL || size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t length = strlen(source);
    if (length >= size) {
        length = size - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

esp_err_t device_message_present(
    const device_message_t *message,
    device_message_error_t *error_out
) {
    set_error(error_out, DEVICE_MESSAGE_OK);
    if (!s_ready) {
        set_error(error_out, DEVICE_MESSAGE_ERR_NOT_INITIALIZED);
        ++s_snapshot.rejected;
        return ESP_ERR_INVALID_STATE;
    }
    if (message == NULL) {
        set_error(error_out, DEVICE_MESSAGE_ERR_INVALID_ARG);
        ++s_snapshot.rejected;
        return ESP_ERR_INVALID_ARG;
    }
    if (!device_message_title_is_valid(message->title) ||
        !device_message_body_is_valid(message->body) ||
        !device_message_severity_is_valid(message->severity)) {
        set_error(error_out, DEVICE_MESSAGE_ERR_INVALID_PAYLOAD);
        ++s_snapshot.rejected;
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t duration = device_message_clamp_duration(
        message->duration_seconds
    );

    bounded_copy(s_last_message.id, sizeof(s_last_message.id), message->id);
    bounded_copy(s_last_message.title, sizeof(s_last_message.title), message->title);
    bounded_copy(s_last_message.body, sizeof(s_last_message.body), message->body);
    bounded_copy(
        s_last_message.severity,
        sizeof(s_last_message.severity),
        message->severity
    );
    bounded_copy(
        s_last_message.category,
        sizeof(s_last_message.category),
        message->category
    );
    s_last_message.duration_seconds = duration;
    s_has_message = true;
    ++s_snapshot.accepted;

    if (has_display()) {
        // The LVGL surface that renders s_last_message is compiled in with the
        // DISPLAY capability. Until it is added this path stays a safe no-op.
        s_snapshot.is_visible = true;
        ++s_snapshot.rendered;
        return ESP_OK;
    }

    // A display-less build still confirms the message so the platform does not
    // retry, and gives the child an audible/visual cue through the LED.
    ++s_snapshot.display_unavailable;
    s_snapshot.is_visible = false;
#if DEVICE_MESSAGE_HAS_LED
    if (led_indicator_is_ready()) {
        const led_indicator_error_t led_result =
            led_indicator_set_state(LED_INDICATOR_STATE_IDLE);
        if (led_result != LED_INDICATOR_OK) {
            ESP_LOGW(TAG, "led cue failed: %s", led_indicator_error_name(led_result));
            ++s_snapshot.feedback_failures;
        }
    }
#endif
    return ESP_OK;
}

bool device_message_last(device_message_t *message_out) {
    if (!s_has_message || message_out == NULL) {
        return false;
    }
    *message_out = s_last_message;
    return true;
}

bool device_message_is_ready(void) {
    return s_ready;
}

device_message_snapshot_t device_message_get_snapshot(void) {
    return s_snapshot;
}

const char *device_message_error_name(device_message_error_t error) {
    switch (error) {
        case DEVICE_MESSAGE_OK:
            return "ok";
        case DEVICE_MESSAGE_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case DEVICE_MESSAGE_ERR_INVALID_ARG:
            return "invalid_argument";
        case DEVICE_MESSAGE_ERR_INVALID_PAYLOAD:
            return "invalid_payload";
        case DEVICE_MESSAGE_ERR_NO_DISPLAY:
            return "no_display";
        case DEVICE_MESSAGE_ERR_BUSY:
            return "busy";
        default:
            return "unknown";
    }
}

static esp_err_t device_message_initialize(void) {
    memset(&s_last_message, 0, sizeof(s_last_message));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_has_message = false;
    s_ready = true;
    return ESP_OK;
}

const module_descriptor_t *device_message_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_message",
        .version = "1.0.0",
        .initialize = device_message_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

esp_err_t device_message_init(void) {
    return device_message_initialize();
}
