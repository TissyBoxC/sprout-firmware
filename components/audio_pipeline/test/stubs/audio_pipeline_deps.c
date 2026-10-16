#include "driver/i2s_std.h"
#include "audio_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int used;
    bool locked;
} mutex_state_t;

static mutex_state_t host_mutex;
static bool host_codec_ready;
static esp_err_t host_new_channel_result;
static esp_err_t host_rx_init_result;
static esp_err_t host_tx_init_result;
static esp_err_t host_enable_result;
static esp_err_t host_disable_result;
static esp_err_t host_read_result;
static esp_err_t host_write_result;
static size_t host_read_bytes;
static size_t host_write_bytes;
static i2s_std_config_t host_last_config;
static int host_new_channel_calls;
static int host_del_channel_calls;
static int host_read_calls;
static int host_write_calls;
static bool host_has_config;
static struct i2s_chan_handle_t host_rx_handle_memory;
static struct i2s_chan_handle_t host_tx_handle_memory;

struct i2s_chan_handle_t {
    int unused;
};

void audio_pipeline_host_reset(void);
void audio_pipeline_host_set_codec_ready(bool ready);
void audio_pipeline_host_set_new_channel_result(esp_err_t result);
void audio_pipeline_host_set_rx_init_result(esp_err_t result);
void audio_pipeline_host_set_tx_init_result(esp_err_t result);
void audio_pipeline_host_set_enable_result(esp_err_t result);
void audio_pipeline_host_set_disable_result(esp_err_t result);
void audio_pipeline_host_set_read_result(esp_err_t result);
void audio_pipeline_host_set_write_result(esp_err_t result);
void audio_pipeline_host_set_read_bytes(size_t bytes);
void audio_pipeline_host_set_write_bytes(size_t bytes);
int audio_pipeline_host_new_channel_calls(void);
int audio_pipeline_host_del_channel_calls(void);
int audio_pipeline_host_read_calls(void);
int audio_pipeline_host_write_calls(void);
bool audio_pipeline_host_last_config(i2s_std_config_t *config_out);

void audio_pipeline_host_reset(void) {
    memset(&host_mutex, 0, sizeof(host_mutex));
    host_codec_ready = true;
    host_new_channel_result = ESP_OK;
    host_rx_init_result = ESP_OK;
    host_tx_init_result = ESP_OK;
    host_enable_result = ESP_OK;
    host_disable_result = ESP_OK;
    host_read_result = ESP_OK;
    host_write_result = ESP_OK;
    host_read_bytes = AUDIO_CODEC_FRAME_PCM_BYTES;
    host_write_bytes = AUDIO_CODEC_FRAME_PCM_BYTES;
    memset(&host_last_config, 0, sizeof(host_last_config));
    host_new_channel_calls = 0;
    host_del_channel_calls = 0;
    host_read_calls = 0;
    host_write_calls = 0;
    host_has_config = false;
    memset(&host_rx_handle_memory, 0, sizeof(host_rx_handle_memory));
    memset(&host_tx_handle_memory, 0, sizeof(host_tx_handle_memory));
}

void audio_pipeline_host_set_codec_ready(bool ready) {
    host_codec_ready = ready;
}

void audio_pipeline_host_set_new_channel_result(esp_err_t result) {
    host_new_channel_result = result;
}

void audio_pipeline_host_set_rx_init_result(esp_err_t result) {
    host_rx_init_result = result;
}

void audio_pipeline_host_set_tx_init_result(esp_err_t result) {
    host_tx_init_result = result;
}

void audio_pipeline_host_set_enable_result(esp_err_t result) {
    host_enable_result = result;
}

void audio_pipeline_host_set_disable_result(esp_err_t result) {
    host_disable_result = result;
}

void audio_pipeline_host_set_read_result(esp_err_t result) {
    host_read_result = result;
}

void audio_pipeline_host_set_write_result(esp_err_t result) {
    host_write_result = result;
}

void audio_pipeline_host_set_read_bytes(size_t bytes) {
    host_read_bytes = bytes;
}

void audio_pipeline_host_set_write_bytes(size_t bytes) {
    host_write_bytes = bytes;
}

int audio_pipeline_host_new_channel_calls(void) {
    return host_new_channel_calls;
}

int audio_pipeline_host_del_channel_calls(void) {
    return host_del_channel_calls;
}

int audio_pipeline_host_read_calls(void) {
    return host_read_calls;
}

int audio_pipeline_host_write_calls(void) {
    return host_write_calls;
}

bool audio_pipeline_host_last_config(i2s_std_config_t *config_out) {
    if (config_out == NULL || !host_has_config) {
        return false;
    }
    *config_out = host_last_config;
    return true;
}

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_FAIL:
            return "ESP_FAIL";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:
            return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED:
            return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:
            return "ESP_ERR_TIMEOUT";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return &host_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    (void)ticks;
    mutex_state_t *state = (mutex_state_t *)semaphore;
    if (state == NULL || state->locked) {
        return pdFALSE;
    }
    state->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    mutex_state_t *state = (mutex_state_t *)semaphore;
    if (state == NULL) {
        return pdFALSE;
    }
    state->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
}

esp_err_t i2s_new_channel(
    const i2s_chan_config_t *config,
    i2s_chan_handle_t *tx_handle,
    i2s_chan_handle_t *rx_handle
) {
    (void)config;
    host_new_channel_calls++;
    if (host_new_channel_result != ESP_OK) {
        return host_new_channel_result;
    }
    if (tx_handle != NULL) {
        *tx_handle = &host_tx_handle_memory;
    }
    if (rx_handle != NULL) {
        *rx_handle = &host_rx_handle_memory;
    }
    return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(
    i2s_chan_handle_t handle,
    const i2s_std_config_t *config
) {
    host_last_config = *config;
    host_has_config = true;
    if (handle == &host_rx_handle_memory) {
        return host_rx_init_result;
    }
    return host_tx_init_result;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t handle) {
    (void)handle;
    return host_enable_result;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t handle) {
    (void)handle;
    return host_disable_result;
}

esp_err_t i2s_channel_read(
    i2s_chan_handle_t handle,
    void *buffer,
    size_t bytes_to_read,
    size_t *bytes_read,
    uint32_t timeout_ms
) {
    (void)handle;
    (void)bytes_to_read;
    (void)timeout_ms;
    host_read_calls++;
    if (host_read_result == ESP_OK && buffer != NULL && bytes_read != NULL) {
        memset(buffer, 0, AUDIO_CODEC_FRAME_PCM_BYTES);
        *bytes_read = host_read_bytes;
    }
    return host_read_result;
}

esp_err_t i2s_channel_write(
    i2s_chan_handle_t handle,
    const void *buffer,
    size_t bytes_to_write,
    size_t *bytes_written,
    uint32_t timeout_ms
) {
    (void)handle;
    (void)buffer;
    (void)bytes_to_write;
    (void)timeout_ms;
    host_write_calls++;
    if (host_write_result == ESP_OK && bytes_written != NULL) {
        *bytes_written = host_write_bytes;
    }
    return host_write_result;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t handle) {
    (void)handle;
    host_del_channel_calls++;
    return ESP_OK;
}

bool audio_codec_is_ready(void) {
    return host_codec_ready;
}
