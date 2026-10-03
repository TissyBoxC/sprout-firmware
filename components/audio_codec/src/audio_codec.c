#include "audio_codec.h"

#include <string.h>

#include "esp_log.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *const TAG = "audio_codec";

// The decoder and encoder handles are owned by this module. The voice session
// calls encode on the capture task and decode on the playback task, so the
// public functions serialise access with one mutex.
static SemaphoreHandle_t audio_codec_mutex;
static void *audio_codec_encoder;
static void *audio_codec_decoder;
static bool audio_codec_ready;

static audio_codec_error_t audio_codec_encode_esp_error(esp_audio_err_t error) {
    switch (error) {
        case ESP_AUDIO_ERR_OK:
            return AUDIO_CODEC_OK;
        case ESP_AUDIO_ERR_DATA_LACK:
            return AUDIO_CODEC_ERR_INVALID_PCM_SIZE;
        case ESP_AUDIO_ERR_BUFF_NOT_ENOUGH:
            return AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL;
        case ESP_AUDIO_ERR_INVALID_PARAMETER:
            return AUDIO_CODEC_ERR_INVALID_ARGUMENT;
        default:
            return AUDIO_CODEC_ERR_ENCODE_FAILED;
    }
}

static audio_codec_error_t audio_codec_decode_esp_error(esp_audio_err_t error) {
    switch (error) {
        case ESP_AUDIO_ERR_OK:
            return AUDIO_CODEC_OK;
        case ESP_AUDIO_ERR_BUFF_NOT_ENOUGH:
            return AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL;
        case ESP_AUDIO_ERR_INVALID_PARAMETER:
            return AUDIO_CODEC_ERR_INVALID_ARGUMENT;
        default:
            return AUDIO_CODEC_ERR_DECODE_FAILED;
    }
}

esp_err_t audio_codec_init(void) {
    if (audio_codec_ready) {
        return ESP_OK;
    }
    if (audio_codec_mutex == NULL) {
        audio_codec_mutex = xSemaphoreCreateMutex();
        if (audio_codec_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    // The encoder is deliberately configured for the one negotiated profile so
    // firmware and gateway never have to renegotiate mid-conversation.
    esp_opus_enc_config_t encoder_config = {
        .sample_rate = AUDIO_CODEC_SAMPLE_RATE_HZ,
        .channel = AUDIO_CODEC_CHANNEL_COUNT,
        .bits_per_sample = 16,
        .bitrate = CONFIG_AUDIO_CODEC_BITRATE,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,
        .complexity = CONFIG_AUDIO_CODEC_COMPLEXITY,
        .enable_fec = false,
#if CONFIG_AUDIO_CODEC_ENABLE_DTX
        .enable_dtx = true,
#else
        .enable_dtx = false,
#endif
        .enable_vbr = false,
    };
    esp_audio_err_t result = esp_opus_enc_open(
        &encoder_config,
        sizeof(encoder_config),
        &audio_codec_encoder
    );
    if (result != ESP_AUDIO_ERR_OK || audio_codec_encoder == NULL) {
        ESP_LOGE(TAG, "opus encoder init failed: %d", (int)result);
        return ESP_FAIL;
    }

    esp_opus_dec_cfg_t decoder_config = {
        .sample_rate = AUDIO_CODEC_SAMPLE_RATE_HZ,
        .channel = AUDIO_CODEC_CHANNEL_COUNT,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS,
        .self_delimited = false,
    };
    result = esp_opus_dec_open(
        &decoder_config,
        sizeof(decoder_config),
        &audio_codec_decoder
    );
    if (result != ESP_AUDIO_ERR_OK || audio_codec_decoder == NULL) {
        ESP_LOGE(TAG, "opus decoder init failed: %d", (int)result);
        esp_opus_enc_close(audio_codec_encoder);
        audio_codec_encoder = NULL;
        return ESP_FAIL;
    }

    audio_codec_ready = true;
    return ESP_OK;
}

bool audio_codec_is_ready(void) {
    return audio_codec_ready;
}

audio_codec_error_t audio_codec_encode_frame(
    const int16_t *pcm,
    size_t pcm_sample_count,
    audio_codec_packet_t *packet_out
) {
    if (!audio_codec_ready || audio_codec_encoder == NULL) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    if (pcm == NULL || packet_out == NULL) {
        return AUDIO_CODEC_ERR_INVALID_ARGUMENT;
    }
    if (pcm_sample_count !=
        AUDIO_CODEC_SAMPLES_PER_FRAME * AUDIO_CODEC_CHANNEL_COUNT) {
        return AUDIO_CODEC_ERR_INVALID_PCM_SIZE;
    }

    if (xSemaphoreTake(audio_codec_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_CODEC_ERR_INTERNAL;
    }

    esp_audio_enc_in_frame_t input = {
        // The encoder API accepts a mutable pointer but does not modify PCM.
        .buffer = (uint8_t *)(uintptr_t)pcm,
        .len = (uint32_t)(pcm_sample_count * sizeof(int16_t)),
    };
    esp_audio_enc_out_frame_t output = {
        .buffer = packet_out->packet,
        .len = sizeof(packet_out->packet),
        .encoded_bytes = 0,
        .pts = 0,
    };
    const esp_audio_err_t result = esp_opus_enc_process(
        audio_codec_encoder,
        &input,
        &output
    );
    xSemaphoreGive(audio_codec_mutex);

    const audio_codec_error_t mapped = audio_codec_encode_esp_error(result);
    if (mapped != AUDIO_CODEC_OK) {
        return mapped;
    }
    if (output.encoded_bytes == 0 ||
        output.encoded_bytes > sizeof(packet_out->packet)) {
        return AUDIO_CODEC_ERR_INVALID_PACKET_SIZE;
    }
    packet_out->packet_size = output.encoded_bytes;
    return AUDIO_CODEC_OK;
}

audio_codec_error_t audio_codec_decode_packet(
    const uint8_t *packet,
    size_t packet_size,
    audio_codec_pcm_frame_t *frame_out
) {
    if (!audio_codec_ready || audio_codec_decoder == NULL) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    if (frame_out == NULL || (packet == NULL && packet_size != 0)) {
        return AUDIO_CODEC_ERR_INVALID_ARGUMENT;
    }
    if (packet_size > AUDIO_CODEC_MAX_PACKET_BYTES) {
        return AUDIO_CODEC_ERR_INVALID_PACKET_SIZE;
    }

    if (xSemaphoreTake(audio_codec_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_CODEC_ERR_INTERNAL;
    }

    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)(uintptr_t)packet,
        .len = (uint32_t)packet_size,
        // An empty payload is an explicit packet-loss signal, so the decoder
        // conceals the gap instead of producing silence with a broken timeline.
        .frame_recover = packet_size == 0
            ? ESP_AUDIO_DEC_RECOVERY_PLC
            : ESP_AUDIO_DEC_RECOVERY_NONE,
    };
    esp_audio_dec_out_frame_t output = {
        .buffer = (uint8_t *)frame_out->pcm,
        .len = sizeof(frame_out->pcm),
        .needed_size = 0,
        .decoded_size = 0,
    };
    const esp_audio_err_t result = esp_opus_dec_decode(
        audio_codec_decoder,
        &raw,
        &output,
        NULL
    );
    xSemaphoreGive(audio_codec_mutex);

    const audio_codec_error_t mapped = audio_codec_decode_esp_error(result);
    if (mapped != AUDIO_CODEC_OK) {
        return mapped;
    }
    if (output.decoded_size != sizeof(frame_out->pcm)) {
        // A short frame would desynchronise playback pacing, so it is treated
        // as a decode failure rather than padded silently.
        return AUDIO_CODEC_ERR_DECODE_FAILED;
    }
    frame_out->pcm_size = output.decoded_size;
    return AUDIO_CODEC_OK;
}

audio_codec_error_t audio_codec_reset(void) {
    if (!audio_codec_ready) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    if (xSemaphoreTake(audio_codec_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_CODEC_ERR_INTERNAL;
    }
    esp_audio_err_t result = esp_opus_enc_reset(audio_codec_encoder);
    if (result == ESP_AUDIO_ERR_OK) {
        result = esp_opus_dec_reset(audio_codec_decoder);
    }
    xSemaphoreGive(audio_codec_mutex);
    return audio_codec_decode_esp_error(result);
}

const char *audio_codec_error_name(audio_codec_error_t error) {
    switch (error) {
        case AUDIO_CODEC_OK:
            return "ok";
        case AUDIO_CODEC_ERR_NOT_INITIALIZED:
            return "audio_codec_not_initialized";
        case AUDIO_CODEC_ERR_INVALID_ARGUMENT:
            return "audio_codec_invalid_argument";
        case AUDIO_CODEC_ERR_INVALID_PACKET_SIZE:
            return "audio_codec_invalid_packet_size";
        case AUDIO_CODEC_ERR_INVALID_PCM_SIZE:
            return "audio_codec_invalid_pcm_size";
        case AUDIO_CODEC_ERR_ENCODE_FAILED:
            return "audio_codec_encode_failed";
        case AUDIO_CODEC_ERR_DECODE_FAILED:
            return "audio_codec_decode_failed";
        case AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL:
            return "audio_codec_output_too_small";
        case AUDIO_CODEC_ERR_INTERNAL:
        default:
            return "audio_codec_internal";
    }
}

const module_descriptor_t *audio_codec_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "audio_codec",
        .version = "1.0.0",
        .initialize = audio_codec_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
