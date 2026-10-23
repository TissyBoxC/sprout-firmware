// Controlled host doubles for every dependency the voice intercom runtime
// touches. Each double records the calls that matter for the child-safety
// contract (microphone opened, audio released, guardian notified) so the
// tests can assert on real behaviour instead of on source text.

#include "voice_intercom_deps.h"

#include <stdlib.h>
#include <string.h>

#include "audio_codec.h"
#include "audio_input.h"
#include "audio_output.h"
#include "audio_pipeline.h"
#include "button_input.h"
#include "config_store.h"
#include "device_identity.h"
#include "device_message.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_indicator.h"
#include "mqtt_client.h"
#include "network_manager.h"
#include "prompt_tone.h"
#include "voice_session.h"

extern "C" {

#define HOST_TOPIC_SIZE 192
#define HOST_PAYLOAD_SIZE 1024
#define HOST_DISPLAY_SIZE 512
#define HOST_KEY_SIZE 32
#define HOST_VALUE_SIZE 128

typedef struct {
    char key[HOST_KEY_SIZE];
    char value[HOST_VALUE_SIZE];
} host_config_entry_t;

static host_config_entry_t host_config_entries[8];
static size_t host_config_count;
static bool host_network_connected = true;
static bool host_identity_available = true;
static bool host_session_ready = true;
static esp_err_t host_session_start_result = ESP_OK;
static bool host_session_active;
static bool host_capture_enabled;
static bool host_capture_missing;
static int host_session_start_calls;
static int host_session_end_calls;
static int host_session_shutdown_calls;
static int host_capture_stop_calls;
static int host_capture_lease_release_calls;
static int host_output_flush_calls;
static int host_prompt_tone_calls;
static int host_display_calls;
static int host_mqtt_init_calls;
static int host_mqtt_stop_calls;
static int host_mqtt_destroy_calls;
static int host_mqtt_subscribe_calls;
static int host_mqtt_publish_calls;
static char host_published_topic[HOST_TOPIC_SIZE];
static char host_published_payload[HOST_PAYLOAD_SIZE];
static char host_display_body[HOST_DISPLAY_SIZE];
static int host_led_state = -1;
static int host_mutex_token;
static esp_mqtt_client_handle_t host_mqtt_handle = nullptr;
static esp_event_handler_t host_mqtt_handler;static TickType_t host_ticks;

static void host_reset_counters(void) {
    host_session_start_calls = 0;
    host_session_end_calls = 0;
    host_session_shutdown_calls = 0;
    host_capture_stop_calls = 0;
    host_capture_lease_release_calls = 0;
    host_output_flush_calls = 0;
    host_prompt_tone_calls = 0;
    host_display_calls = 0;
    host_mqtt_init_calls = 0;
    host_mqtt_stop_calls = 0;
    host_mqtt_destroy_calls = 0;
    host_mqtt_subscribe_calls = 0;
    host_mqtt_publish_calls = 0;
    host_published_topic[0] = '\0';
    host_published_payload[0] = '\0';
    host_display_body[0] = '\0';
    host_led_state = -1;
    host_mqtt_handler = nullptr;
    host_mqtt_handle = nullptr;
    host_ticks = 0;
}

void voice_intercom_host_reset(void) {
    host_config_count = 0;
    host_network_connected = true;
    host_identity_available = true;
    host_session_ready = true;
    host_session_start_result = ESP_OK;
    host_session_active = false;
    host_capture_enabled = false;
    host_capture_missing = false;
    host_reset_counters();
    esp_http_client_host_reset();
}
void voice_intercom_host_advance_ticks(uint32_t ticks) {
    host_ticks += ticks;
}

void voice_intercom_host_set_network_connected(bool is_connected) {
    host_network_connected = is_connected;
}

void voice_intercom_host_set_identity_available(bool is_available) {
    host_identity_available = is_available;
}

void voice_intercom_host_set_session_ready(bool is_ready) {
    host_session_ready = is_ready;
}

void voice_intercom_host_set_session_start_result(int result) {
    host_session_start_result = result;
}

void voice_intercom_host_set_capture_enabled(bool is_enabled) {
    host_capture_enabled = is_enabled;
}

void voice_intercom_host_set_capture_drops_after_start(void) {
    // The session reports success but never takes the capture lease; the
    // module must refuse to mark the call active.
    host_capture_missing = true;
}

void voice_intercom_host_set_capture_missing(void) {
    host_capture_missing = true;
    host_capture_enabled = false;
}

void voice_intercom_host_set_broker_url(const char *broker_url) {
    host_config_count = 0;
    if (broker_url == nullptr) {
        return;
    }
    const char *keys[] = {"mqtt_url", "mqtt_ca", "mqtt_cert", "mqtt_key"};
    const char *values[] = {broker_url, "CA", "CERT", "KEY"};
    for (size_t index = 0; index < 4; ++index) {
        strncpy(
            host_config_entries[index].key,
            keys[index],
            HOST_KEY_SIZE - 1
        );
        strncpy(
            host_config_entries[index].value,
            values[index],
            HOST_VALUE_SIZE - 1
        );
    }
    host_config_count = 4;
}

void voice_intercom_host_deliver_mqtt_data(const char *payload, int length) {
    if (host_mqtt_handler == nullptr || payload == nullptr) {
        return;
    }
    esp_mqtt_event_t event = {};
    event.data = const_cast<char *>(payload);
    event.data_len = length;
    event.total_data_len = length;
    host_mqtt_handler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
}

int voice_intercom_host_session_start_calls(void) {
    return host_session_start_calls;
}

int voice_intercom_host_session_end_calls(void) {
    return host_session_end_calls;
}

int voice_intercom_host_session_shutdown_calls(void) {
    return host_session_shutdown_calls;
}

int voice_intercom_host_capture_stop_calls(void) {
    return host_capture_stop_calls;
}

int voice_intercom_host_capture_lease_release_calls(void) {
    return host_capture_lease_release_calls;
}

int voice_intercom_host_output_flush_calls(void) {
    return host_output_flush_calls;
}

int voice_intercom_host_prompt_tone_calls(void) {
    return host_prompt_tone_calls;
}

int voice_intercom_host_display_calls(void) {
    return host_display_calls;
}

int voice_intercom_host_mqtt_init_calls(void) {
    return host_mqtt_init_calls;
}

int voice_intercom_host_mqtt_stop_calls(void) {
    return host_mqtt_stop_calls;
}

int voice_intercom_host_mqtt_destroy_calls(void) {
    return host_mqtt_destroy_calls;
}

int voice_intercom_host_mqtt_subscribe_calls(void) {
    return host_mqtt_subscribe_calls;
}

int voice_intercom_host_mqtt_publish_calls(void) {
    return host_mqtt_publish_calls;
}

esp_mqtt_client_handle_t voice_intercom_host_mqtt_client(void) {
    return host_mqtt_handle;
}

const char *voice_intercom_host_mqtt_published_topic(void) {
    return host_published_topic;
}

const char *voice_intercom_host_mqtt_published_payload(void) {
    return host_published_payload;
}

const char *voice_intercom_host_display_body(void) {
    return host_display_body;
}

int voice_intercom_host_led_state(void) {
    return host_led_state;
}

// ------------------------------------------------------------ esp_err

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_TIMEOUT:
            return "ESP_ERR_TIMEOUT";
        default:
            return "ESP_FAIL";
    }
}

// ------------------------------------------------------------ FreeRTOS

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return &host_mutex_token;
}

int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)semaphore;
    (void)timeout;
    return 1;
}

int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return 1;
}

int xTaskCreate(
    TaskEntry_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    unsigned int priority,
    TaskHandle_t *task_handle
) {
    (void)task;
    (void)name;
    (void)stack_size;
    (void)argument;
    (void)priority;
    if (task_handle != nullptr) {
        *task_handle = (TaskHandle_t)(size_t)1;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    host_ticks += ticks;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

TickType_t xTaskGetTickCount(void) {
    return host_ticks;
}

// ------------------------------------------------------------ config store

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    if (key == nullptr || output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < host_config_count; ++index) {
        if (strcmp(host_config_entries[index].key, key) == 0) {
            strncpy(output, host_config_entries[index].value, output_size - 1);
            output[output_size - 1] = '\0';
            return ESP_OK;
        }
    }
    output[0] = '\0';
    return ESP_ERR_NOT_FOUND;
}

// ------------------------------------------------------------ platform

esp_err_t device_identity_copy(char *device_id_out, size_t size) {
    if (device_id_out == nullptr || size == 0 || !host_identity_available) {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(device_id_out, "device-0001", size - 1);
    device_id_out[size - 1] = '\0';
    return ESP_OK;
}

network_manager_state_t network_manager_get_state(void) {
    return host_network_connected
        ? NETWORK_MANAGER_STATE_CONNECTED
        : NETWORK_MANAGER_STATE_DISCONNECTED;
}

esp_err_t audio_input_stop(void) {
    host_capture_stop_calls += 1;
    host_capture_enabled = false;
    return ESP_OK;
}

esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out) {
    if (snapshot_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->is_running = host_capture_enabled;
    return ESP_OK;
}

esp_err_t audio_pipeline_capture_release(
    audio_pipeline_capture_owner_t owner
) {
    (void)owner;
    host_capture_lease_release_calls += 1;
    host_capture_enabled = false;
    return ESP_OK;
}

audio_output_error_t audio_output_flush(
    audio_output_priority_t max_priority,
    bool include_safety,
    size_t *discarded_frames_out
) {
    (void)max_priority;
    (void)include_safety;
    if (discarded_frames_out != nullptr) {
        *discarded_frames_out = 0;
    }
    host_output_flush_calls += 1;
    return AUDIO_OUTPUT_OK;
}

audio_output_error_t audio_output_submit(
    const audio_codec_pcm_frame_t *frame,
    audio_output_priority_t priority
) {
    (void)frame;
    (void)priority;
    return AUDIO_OUTPUT_OK;
}

led_indicator_error_t led_indicator_set_state(led_indicator_state_t state) {
    host_led_state = (int)state;
    return LED_INDICATOR_OK;
}

prompt_tone_error_t prompt_tone_play(prompt_tone_t tone) {
    (void)tone;
    host_prompt_tone_calls += 1;
    return PROMPT_TONE_OK;
}

esp_err_t device_message_present(
    const device_message_t *message,
    device_message_error_t *error_out
) {
    if (error_out != nullptr) {
        *error_out = DEVICE_MESSAGE_OK;
    }
    if (message == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    host_display_calls += 1;
    strncpy(host_display_body, message->body, HOST_DISPLAY_SIZE - 1);
    host_display_body[HOST_DISPLAY_SIZE - 1] = '\0';
    return ESP_OK;
}

// ------------------------------------------------------------ voice session

bool voice_session_is_ready(void) {
    return host_session_ready;
}

esp_err_t voice_session_start_session(
    const char *stream_id,
    const char *session_id
) {
    (void)stream_id;
    (void)session_id;
    host_session_start_calls += 1;
    if (host_session_start_result != ESP_OK) {
        return host_session_start_result;
    }
    host_session_active = true;
    if (!host_capture_missing) {
        // Starting a real session takes the microphone lease.
        host_capture_enabled = true;
    }
    return ESP_OK;
}

esp_err_t voice_session_end_session(const char *reason) {
    (void)reason;
    host_session_end_calls += 1;
    host_session_active = false;
    host_capture_enabled = false;
    return ESP_OK;
}

esp_err_t voice_session_get_snapshot(voice_session_snapshot_t *snapshot_out) {
    if (snapshot_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->is_initialized = host_session_ready;
    snapshot_out->has_active_session = host_session_active;
    snapshot_out->state = host_session_active
        ? VOICE_SESSION_STATE_LISTENING
        : VOICE_SESSION_STATE_IDLE;
    return ESP_OK;
}

void voice_session_shutdown(void) {
    host_session_shutdown_calls += 1;
    host_session_active = false;
    host_capture_enabled = false;
}

// ------------------------------------------------------------ MQTT

struct esp_mqtt_client {
    int unused;
};

static esp_mqtt_client host_client_storage;

esp_mqtt_client_handle_t esp_mqtt_client_init(
    const esp_mqtt_client_config_t *config
) {
    (void)config;
    host_mqtt_init_calls += 1;
    host_mqtt_handle = &host_client_storage;
    return host_mqtt_handle;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
    (void)client;
    // The real client reports the connect event asynchronously; the double
    // reports it synchronously so the module subscribes before the test
    // delivers a command.
    if (host_mqtt_handler != nullptr) {
        host_mqtt_handler(nullptr, nullptr, MQTT_EVENT_CONNECTED, nullptr);
    }
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
    (void)client;
    host_mqtt_stop_calls += 1;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
    (void)client;
    host_mqtt_destroy_calls += 1;
    host_mqtt_handle = nullptr;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t client,
    int32_t event,
    esp_event_handler_t event_handler,
    void *event_handler_arg
) {
    (void)client;
    (void)event;
    (void)event_handler_arg;
    host_mqtt_handler = event_handler;
    return ESP_OK;
}

int esp_mqtt_client_subscribe_single(
    esp_mqtt_client_handle_t client,
    const char *topic,
    int qos
) {
    (void)client;
    (void)topic;
    (void)qos;
    host_mqtt_subscribe_calls += 1;
    return 1;
}

int esp_mqtt_client_publish(
    esp_mqtt_client_handle_t client,
    const char *topic,
    const char *data,
    int len,
    int qos,
    int retain
) {
    (void)client;
    (void)qos;
    (void)retain;
    host_mqtt_publish_calls += 1;
    if (topic != nullptr) {
        strncpy(host_published_topic, topic, HOST_TOPIC_SIZE - 1);
    }
    if (data != nullptr && len > 0) {
        const size_t bounded = (size_t)len < HOST_PAYLOAD_SIZE - 1
            ? (size_t)len
            : HOST_PAYLOAD_SIZE - 1;
        memcpy(host_published_payload, data, bounded);
        host_published_payload[bounded] = '\0';
    }
    return 1;
}

// --- device binding client ------------------------------------------------

esp_err_t device_binding_client_copy_session_token(
    char *output,
    size_t output_size
) {
    if (output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *token = "session-token-1";
    const size_t length = strlen(token);
    if (length >= output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, token, length + 1);
    return ESP_OK;
}

esp_err_t device_binding_client_get_platform_base_url(
    char *output,
    size_t output_size
) {
    if (output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *base_url = "https://api.example.invalid";
    const size_t length = strlen(base_url);
    if (length >= output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, base_url, length + 1);
    return ESP_OK;
}

// --- HTTP client ----------------------------------------------------------

// The real bundle installs the CA store on the TLS config. The host build has no
// TLS stack, so the attach point only has to exist for the client code to link.
int esp_crt_bundle_attach(void *conf) {
    (void)conf;
    return 0;
}

// Records every call-state report so a test can assert that a call the child
// refused is reported as a decline and that no report carries call audio.
esp_err_t esp_http_client_init_result(void);
int esp_http_client_host_perform_count(void);
const char *esp_http_client_host_perform_url(size_t index);
const char *esp_http_client_host_perform_body(size_t index);
void esp_http_client_host_reset(void);

#define kHostHttpCapacity 16
#define kHostUrlSize 256
#define kHostBodySize 128

typedef struct host_http_request_t {
    char url[kHostUrlSize];
    char body[kHostBodySize];
} host_http_request_t;

// The stub header forward-declares this type the way the real one does, so the
// host build defines the body here before any dereference.
struct esp_http_client {
    host_http_request_t *request;
};

static host_http_request_t g_host_http_requests[kHostHttpCapacity];
static size_t g_host_http_count;

esp_http_client_handle_t esp_http_client_init(
    const esp_http_client_config_t *config
) {
    if (config == nullptr || g_host_http_count >= kHostHttpCapacity) {
        return nullptr;
    }
    esp_http_client *client =
        (esp_http_client *)calloc(1, sizeof(esp_http_client));
    if (client == nullptr) {
        return nullptr;
    }
    host_http_request_t *request = &g_host_http_requests[g_host_http_count];
    memset(request, 0, sizeof(*request));
    if (config->url != nullptr) {
        strncpy(request->url, config->url, kHostUrlSize - 1);
    }
    client->request = request;
    g_host_http_count += 1;
    return (esp_http_client_handle_t)client;
}

esp_err_t esp_http_client_set_header(
    esp_http_client_handle_t client,
    const char *key,
    const char *value
) {
    if (client == nullptr || key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(
    esp_http_client_handle_t client,
    const char *data,
    int len
) {
    if (client == nullptr || data == nullptr || len <= 0 ||
        (size_t)len >= kHostBodySize) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(client->request->body, data, (size_t)len);
    client->request->body[len] = '\0';
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    return client == nullptr ? ESP_ERR_INVALID_ARG : ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return client == nullptr ? 0 : 200;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    free(client);
    return ESP_OK;
}

esp_err_t esp_http_client_init_result(void) {
    return ESP_OK;
}

int esp_http_client_host_perform_count(void) {
    return (int)g_host_http_count;
}

const char *esp_http_client_host_perform_url(size_t index) {
    return index < g_host_http_count ? g_host_http_requests[index].url : "";
}

const char *esp_http_client_host_perform_body(size_t index) {
    return index < g_host_http_count ? g_host_http_requests[index].body : "";
}

void esp_http_client_host_reset(void) {
    memset(g_host_http_requests, 0, sizeof(g_host_http_requests));
    g_host_http_count = 0;
}

}  // extern "C"
