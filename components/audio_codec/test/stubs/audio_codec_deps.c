#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "freertos/semphr.h"

#include <string.h>

// Host stand-ins for the Opus codec and the FreeRTOS mutex. Every failure path
// in audio_codec.c is reachable from the test through these hooks.
static esp_audio_err_t host_enc_open_result = ESP_AUDIO_ERR_OK;
static esp_audio_err_t host_dec_open_result = ESP_AUDIO_ERR_OK;
static esp_audio_err_t host_enc_process_result = ESP_AUDIO_ERR_OK;
static esp_audio_err_t host_dec_decode_result = ESP_AUDIO_ERR_OK;
static esp_audio_err_t host_enc_reset_result = ESP_AUDIO_ERR_OK;
static esp_audio_err_t host_dec_reset_result = ESP_AUDIO_ERR_OK;
static bool host_enc_open_returns_null_handle;
static bool host_dec_open_returns_null_handle;
static uint32_t host_encoded_bytes = 40;
static uint32_t host_decoded_bytes;
static bool host_create_mutex_fails;
static bool host_take_mutex_fails;

static int host_encoder;
static int host_decoder;
static int host_mutex;

static esp_opus_enc_config_t host_last_encoder_config;
static esp_opus_dec_cfg_t host_last_decoder_config;
static esp_audio_dec_in_raw_t host_last_raw;
static uint32_t host_last_enc_input_len;
static int host_enc_open_calls;
static int host_dec_open_calls;
static int host_enc_close_calls;
static int host_dec_close_calls;
static int host_enc_reset_calls;
static int host_dec_reset_calls;

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (host_create_mutex_fails) {
        return NULL;
    }
    return &host_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, UBaseType_t timeout) {
    (void)semaphore;
    (void)timeout;
    return host_take_mutex_fails ? pdFALSE : pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

esp_audio_err_t esp_opus_enc_open(void *cfg, uint32_t cfg_sz, void **enc_hd) {
    ++host_enc_open_calls;
    if (cfg_sz == sizeof(esp_opus_enc_config_t)) {
        memcpy(&host_last_encoder_config, cfg, sizeof(host_last_encoder_config));
    }
    *enc_hd = host_enc_open_returns_null_handle ? NULL : &host_encoder;
    return host_enc_open_result;
}

esp_audio_err_t esp_opus_enc_process(
    void *enc_hd,
    esp_audio_enc_in_frame_t *in_frame,
    esp_audio_enc_out_frame_t *out_frame
) {
    (void)enc_hd;
    host_last_enc_input_len = in_frame->len;
    if (host_enc_process_result == ESP_AUDIO_ERR_OK) {
        out_frame->encoded_bytes = host_encoded_bytes;
    }
    return host_enc_process_result;
}

esp_audio_err_t esp_opus_enc_reset(void *enc_hd) {
    (void)enc_hd;
    ++host_enc_reset_calls;
    return host_enc_reset_result;
}

void esp_opus_enc_close(void *enc_hd) {
    (void)enc_hd;
    ++host_enc_close_calls;
}

esp_audio_err_t esp_opus_dec_open(void *cfg, uint32_t cfg_sz, void **dec_handle) {
    ++host_dec_open_calls;
    if (cfg_sz == sizeof(esp_opus_dec_cfg_t)) {
        memcpy(&host_last_decoder_config, cfg, sizeof(host_last_decoder_config));
    }
    *dec_handle = host_dec_open_returns_null_handle ? NULL : &host_decoder;
    return host_dec_open_result;
}

esp_audio_err_t esp_opus_dec_decode(
    void *dec_handle,
    esp_audio_dec_in_raw_t *raw,
    esp_audio_dec_out_frame_t *frame,
    void *dec_info
) {
    (void)dec_handle;
    (void)dec_info;
    host_last_raw = *raw;
    if (host_dec_decode_result == ESP_AUDIO_ERR_OK) {
        frame->decoded_size = host_decoded_bytes;
    }
    return host_dec_decode_result;
}

esp_audio_err_t esp_opus_dec_reset(void *dec_handle) {
    (void)dec_handle;
    ++host_dec_reset_calls;
    return host_dec_reset_result;
}

void esp_opus_dec_close(void *dec_handle) {
    (void)dec_handle;
    ++host_dec_close_calls;
}

// Test hooks.
void audio_codec_host_reset(void) {
    host_enc_open_result = ESP_AUDIO_ERR_OK;
    host_dec_open_result = ESP_AUDIO_ERR_OK;
    host_enc_process_result = ESP_AUDIO_ERR_OK;
    host_dec_decode_result = ESP_AUDIO_ERR_OK;
    host_enc_reset_result = ESP_AUDIO_ERR_OK;
    host_dec_reset_result = ESP_AUDIO_ERR_OK;
    host_enc_open_returns_null_handle = false;
    host_dec_open_returns_null_handle = false;
    host_create_mutex_fails = false;
    host_take_mutex_fails = false;
    host_encoded_bytes = 40;
    host_decoded_bytes = 0;
    host_enc_open_calls = 0;
    host_dec_open_calls = 0;
    host_enc_close_calls = 0;
    host_dec_close_calls = 0;
    host_enc_reset_calls = 0;
    host_dec_reset_calls = 0;
    memset(&host_last_encoder_config, 0, sizeof(host_last_encoder_config));
    memset(&host_last_decoder_config, 0, sizeof(host_last_decoder_config));
    memset(&host_last_raw, 0, sizeof(host_last_raw));
    host_last_enc_input_len = 0;
}

void audio_codec_host_set_encoder_open_result(esp_audio_err_t result) {
    host_enc_open_result = result;
}

void audio_codec_host_set_decoder_open_result(esp_audio_err_t result) {
    host_dec_open_result = result;
}

void audio_codec_host_set_encode_result(esp_audio_err_t result) {
    host_enc_process_result = result;
}

void audio_codec_host_set_decode_result(esp_audio_err_t result) {
    host_dec_decode_result = result;
}

void audio_codec_host_set_encoder_reset_result(esp_audio_err_t result) {
    host_enc_reset_result = result;
}

void audio_codec_host_set_decoder_reset_result(esp_audio_err_t result) {
    host_dec_reset_result = result;
}

void audio_codec_host_set_encoder_open_null_handle(bool returns_null) {
    host_enc_open_returns_null_handle = returns_null;
}

void audio_codec_host_set_decoder_open_null_handle(bool returns_null) {
    host_dec_open_returns_null_handle = returns_null;
}

void audio_codec_host_set_encoded_bytes(uint32_t encoded_bytes) {
    host_encoded_bytes = encoded_bytes;
}

void audio_codec_host_set_decoded_bytes(uint32_t decoded_bytes) {
    host_decoded_bytes = decoded_bytes;
}

void audio_codec_host_set_create_mutex_fails(bool fails) {
    host_create_mutex_fails = fails;
}

void audio_codec_host_set_take_mutex_fails(bool fails) {
    host_take_mutex_fails = fails;
}

esp_opus_enc_config_t audio_codec_host_last_encoder_config(void) {
    return host_last_encoder_config;
}

esp_opus_dec_cfg_t audio_codec_host_last_decoder_config(void) {
    return host_last_decoder_config;
}

esp_audio_dec_recovery_t audio_codec_host_last_frame_recovery(void) {
    return host_last_raw.frame_recover;
}

uint32_t audio_codec_host_last_raw_len(void) {
    return host_last_raw.len;
}

uint32_t audio_codec_host_last_encode_input_len(void) {
    return host_last_enc_input_len;
}

int audio_codec_host_encoder_open_calls(void) {
    return host_enc_open_calls;
}

int audio_codec_host_decoder_open_calls(void) {
    return host_dec_open_calls;
}

int audio_codec_host_encoder_close_calls(void) {
    return host_enc_close_calls;
}

int audio_codec_host_decoder_close_calls(void) {
    return host_dec_close_calls;
}

int audio_codec_host_encoder_reset_calls(void) {
    return host_enc_reset_calls;
}

int audio_codec_host_decoder_reset_calls(void) {
    return host_dec_reset_calls;
}
