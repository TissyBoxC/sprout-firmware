#include "audio_pipeline.h"
#include "freertos/semphr.h"

#include <string.h>

// Host stand-ins for the FreeRTOS mutex and the direct-to-speaker fallback.
// The playback queue and mixer stay out of this build so the minimal-firmware
// path is the one under test.
static bool host_create_mutex_fails;
static bool host_take_mutex_fails;
static bool host_pipeline_ready = true;
static esp_err_t host_set_playing_result = ESP_OK;
static audio_codec_error_t host_play_frame_result = AUDIO_CODEC_OK;

static int host_mutex;
static int host_set_playing_calls;
static int host_play_frame_calls;
static size_t host_played_bytes;
static uint32_t host_frame_checksum;

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

bool audio_pipeline_is_ready(void) {
    return host_pipeline_ready;
}

esp_err_t audio_pipeline_set_playing(bool is_playing) {
    (void)is_playing;
    ++host_set_playing_calls;
    return host_set_playing_result;
}

audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
) {
    ++host_play_frame_calls;
    if (frame != NULL) {
        host_played_bytes += frame->pcm_size;
        for (size_t index = 0; index < frame->pcm_size / sizeof(int16_t);
             ++index) {
            host_frame_checksum =
                host_frame_checksum * 31U + (uint32_t)(uint16_t)frame->pcm[index];
        }
    }
    return host_play_frame_result;
}

// Test hooks.
void prompt_tone_host_reset(void) {
    host_create_mutex_fails = false;
    host_take_mutex_fails = false;
    host_pipeline_ready = true;
    host_set_playing_result = ESP_OK;
    host_play_frame_result = AUDIO_CODEC_OK;
    host_set_playing_calls = 0;
    host_play_frame_calls = 0;
    host_played_bytes = 0;
    host_frame_checksum = 0;
}

void prompt_tone_host_set_create_mutex_fails(bool fails) {
    host_create_mutex_fails = fails;
}

void prompt_tone_host_set_take_mutex_fails(bool fails) {
    host_take_mutex_fails = fails;
}

void prompt_tone_host_set_pipeline_ready(bool ready) {
    host_pipeline_ready = ready;
}

void prompt_tone_host_set_set_playing_result(esp_err_t result) {
    host_set_playing_result = result;
}

void prompt_tone_host_set_play_frame_result(audio_codec_error_t result) {
    host_play_frame_result = result;
}

int prompt_tone_host_set_playing_calls(void) {
    return host_set_playing_calls;
}

int prompt_tone_host_play_frame_calls(void) {
    return host_play_frame_calls;
}

size_t prompt_tone_host_played_bytes(void) {
    return host_played_bytes;
}

uint32_t prompt_tone_host_frame_checksum(void) {
    return host_frame_checksum;
}
