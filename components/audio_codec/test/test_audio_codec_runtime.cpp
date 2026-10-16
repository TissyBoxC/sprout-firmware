#include "audio_codec.h"

#include "esp_audio_types.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"

#include <cassert>
#include <cstring>

void audio_codec_host_reset(void);
void audio_codec_host_set_encoder_open_result(esp_audio_err_t result);
void audio_codec_host_set_decoder_open_result(esp_audio_err_t result);
void audio_codec_host_set_encode_result(esp_audio_err_t result);
void audio_codec_host_set_decode_result(esp_audio_err_t result);
void audio_codec_host_set_encoder_reset_result(esp_audio_err_t result);
void audio_codec_host_set_decoder_reset_result(esp_audio_err_t result);
void audio_codec_host_set_encoder_open_null_handle(bool returns_null);
void audio_codec_host_set_decoder_open_null_handle(bool returns_null);
void audio_codec_host_set_encoded_bytes(uint32_t encoded_bytes);
void audio_codec_host_set_decoded_bytes(uint32_t decoded_bytes);
void audio_codec_host_set_create_mutex_fails(bool fails);
void audio_codec_host_set_take_mutex_fails(bool fails);
esp_opus_enc_config_t audio_codec_host_last_encoder_config(void);
esp_opus_dec_cfg_t audio_codec_host_last_decoder_config(void);
esp_audio_dec_recovery_t audio_codec_host_last_frame_recovery(void);
uint32_t audio_codec_host_last_raw_len(void);
uint32_t audio_codec_host_last_encode_input_len(void);
int audio_codec_host_encoder_open_calls(void);
int audio_codec_host_decoder_open_calls(void);
int audio_codec_host_encoder_close_calls(void);
int audio_codec_host_decoder_close_calls(void);
int audio_codec_host_encoder_reset_calls(void);
int audio_codec_host_decoder_reset_calls(void);

namespace {

void test_init_uses_the_negotiated_profile_and_is_idempotent() {
    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);
    assert(audio_codec_is_ready());

    // Both directions were opened exactly once with the contract profile.
    assert(audio_codec_host_encoder_open_calls() == 1);
    assert(audio_codec_host_decoder_open_calls() == 1);
    const esp_opus_enc_config_t encoder =
        audio_codec_host_last_encoder_config();
    assert(encoder.sample_rate == AUDIO_CODEC_SAMPLE_RATE_HZ);
    assert(encoder.channel == AUDIO_CODEC_CHANNEL_COUNT);
    assert(encoder.bits_per_sample == 16);
    assert(encoder.frame_duration == ESP_OPUS_ENC_FRAME_DURATION_20_MS);
    assert(encoder.application_mode == ESP_OPUS_ENC_APPLICATION_VOIP);
    assert(!encoder.enable_fec);
    // This host build compiles with CONFIG_AUDIO_CODEC_ENABLE_DTX=1.
    assert(encoder.enable_dtx);
    assert(!encoder.enable_vbr);

    const esp_opus_dec_cfg_t decoder = audio_codec_host_last_decoder_config();
    assert(decoder.sample_rate == AUDIO_CODEC_SAMPLE_RATE_HZ);
    assert(decoder.channel == AUDIO_CODEC_CHANNEL_COUNT);
    assert(decoder.frame_duration == ESP_OPUS_DEC_FRAME_DURATION_20_MS);
    assert(!decoder.self_delimited);

    // Idempotent init must not reopen either codec.
    assert(audio_codec_init() == ESP_OK);
    assert(audio_codec_host_encoder_open_calls() == 1);
    assert(audio_codec_host_decoder_open_calls() == 1);
}

void test_encoder_open_failure_leaves_module_unready() {
    audio_codec_host_reset();
    audio_codec_host_set_encoder_open_result(ESP_AUDIO_ERR_MEM_LACK);
    assert(audio_codec_init() == ESP_FAIL);
    assert(!audio_codec_is_ready());

    // A success result that yields a NULL handle is treated as a failure.
    audio_codec_host_reset();
    audio_codec_host_set_encoder_open_null_handle(true);
    assert(audio_codec_init() == ESP_FAIL);
    assert(!audio_codec_is_ready());
}

void test_decoder_open_failure_closes_the_encoder() {
    audio_codec_host_reset();
    audio_codec_host_set_decoder_open_result(ESP_AUDIO_ERR_INVALID_PARAMETER);
    assert(audio_codec_init() == ESP_FAIL);
    assert(!audio_codec_is_ready());
    // The partially constructed encoder must not leak.
    assert(audio_codec_host_encoder_close_calls() == 1);

    audio_codec_host_reset();
    audio_codec_host_set_decoder_open_null_handle(true);
    assert(audio_codec_init() == ESP_FAIL);
    assert(!audio_codec_is_ready());
    assert(audio_codec_host_encoder_close_calls() == 1);
}

void test_mutex_creation_failure_is_reported() {
    audio_codec_host_reset();
    audio_codec_host_set_create_mutex_fails(true);
    assert(audio_codec_init() == ESP_ERR_NO_MEM);
    assert(!audio_codec_is_ready());
    assert(audio_codec_host_encoder_open_calls() == 0);
}

void test_encode_validates_arguments_and_sizes() {
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME] = {0};
    audio_codec_packet_t packet{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    assert(
        audio_codec_encode_frame(nullptr, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_INVALID_ARGUMENT
    );
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, nullptr) ==
        AUDIO_CODEC_ERR_INVALID_ARGUMENT
    );
    // One sample too few must be rejected before the encoder is touched.
    assert(
        audio_codec_encode_frame(
            pcm,
            AUDIO_CODEC_SAMPLES_PER_FRAME - 1,
            &packet
        ) == AUDIO_CODEC_ERR_INVALID_PCM_SIZE
    );
    assert(
        audio_codec_encode_frame(
            pcm,
            AUDIO_CODEC_SAMPLES_PER_FRAME + 1,
            &packet
        ) == AUDIO_CODEC_ERR_INVALID_PCM_SIZE
    );
}

void test_encode_maps_engine_errors_and_reports_size() {
    int16_t pcm[AUDIO_CODEC_FRAME_PCM_BYTES / (int)sizeof(int16_t)] = {0};
    audio_codec_packet_t packet{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    // A clean encode publishes the engine byte count.
    audio_codec_host_set_encoded_bytes(37);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_OK
    );
    assert(packet.packet_size == 37);
    // PCM is passed through as bytes, not samples.
    assert(audio_codec_host_last_encode_input_len() == AUDIO_CODEC_FRAME_PCM_BYTES);

    audio_codec_host_set_encode_result(ESP_AUDIO_ERR_DATA_LACK);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_INVALID_PCM_SIZE
    );
    audio_codec_host_set_encode_result(ESP_AUDIO_ERR_BUFF_NOT_ENOUGH);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL
    );
    audio_codec_host_set_encode_result(ESP_AUDIO_ERR_INVALID_PARAMETER);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_INVALID_ARGUMENT
    );
    audio_codec_host_set_encode_result(ESP_AUDIO_ERR_FAIL);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_ENCODE_FAILED
    );
}

void test_encode_rejects_an_empty_or_oversized_engine_result() {
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME] = {0};
    audio_codec_packet_t packet{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    // A zero-length packet would desynchronise the stream.
    audio_codec_host_set_encoded_bytes(0);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_INVALID_PACKET_SIZE
    );
    // A packet larger than the caller's buffer must not be reported as success.
    audio_codec_host_set_encoded_bytes(AUDIO_CODEC_MAX_PACKET_BYTES + 1);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_INVALID_PACKET_SIZE
    );
}

void test_decode_validates_arguments_and_sizes() {
    uint8_t packet[AUDIO_CODEC_MAX_PACKET_BYTES] = {0};
    audio_codec_pcm_frame_t frame{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    assert(
        audio_codec_decode_packet(packet, 10, nullptr) ==
        AUDIO_CODEC_ERR_INVALID_ARGUMENT
    );
    // A NULL buffer is only legal for the empty packet-loss signal.
    assert(
        audio_codec_decode_packet(nullptr, 10, &frame) ==
        AUDIO_CODEC_ERR_INVALID_ARGUMENT
    );
    assert(
        audio_codec_decode_packet(packet, AUDIO_CODEC_MAX_PACKET_BYTES + 1, &frame) ==
        AUDIO_CODEC_ERR_INVALID_PACKET_SIZE
    );
}

void test_decode_marks_packet_loss_and_requires_a_full_frame() {
    uint8_t packet[AUDIO_CODEC_MAX_PACKET_BYTES] = {0};
    audio_codec_pcm_frame_t frame{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    // A normal frame decodes to exactly one PCM frame.
    audio_codec_host_set_decoded_bytes(AUDIO_CODEC_FRAME_PCM_BYTES);
    assert(audio_codec_decode_packet(packet, 32, &frame) == AUDIO_CODEC_OK);
    assert(frame.pcm_size == AUDIO_CODEC_FRAME_PCM_BYTES);
    assert(
        audio_codec_host_last_frame_recovery() == ESP_AUDIO_DEC_RECOVERY_NONE
    );

    // An empty packet is an explicit concealment request.
    assert(audio_codec_decode_packet(nullptr, 0, &frame) == AUDIO_CODEC_OK);
    assert(
        audio_codec_host_last_frame_recovery() == ESP_AUDIO_DEC_RECOVERY_PLC
    );
    assert(audio_codec_host_last_raw_len() == 0);

    // A short decoded frame breaks playback pacing and is a decode failure.
    audio_codec_host_set_decoded_bytes(AUDIO_CODEC_FRAME_PCM_BYTES - 2);
    assert(
        audio_codec_decode_packet(packet, 32, &frame) ==
        AUDIO_CODEC_ERR_DECODE_FAILED
    );
    audio_codec_host_set_decoded_bytes(AUDIO_CODEC_FRAME_PCM_BYTES + 2);
    assert(
        audio_codec_decode_packet(packet, 32, &frame) ==
        AUDIO_CODEC_ERR_DECODE_FAILED
    );
}

void test_decode_maps_engine_errors() {
    uint8_t packet[AUDIO_CODEC_MAX_PACKET_BYTES] = {0};
    audio_codec_pcm_frame_t frame{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    audio_codec_host_set_decode_result(ESP_AUDIO_ERR_BUFF_NOT_ENOUGH);
    assert(
        audio_codec_decode_packet(packet, 32, &frame) ==
        AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL
    );
    audio_codec_host_set_decode_result(ESP_AUDIO_ERR_INVALID_PARAMETER);
    assert(
        audio_codec_decode_packet(packet, 32, &frame) ==
        AUDIO_CODEC_ERR_INVALID_ARGUMENT
    );
    audio_codec_host_set_decode_result(ESP_AUDIO_ERR_FAIL);
    assert(
        audio_codec_decode_packet(packet, 32, &frame) ==
        AUDIO_CODEC_ERR_DECODE_FAILED
    );
}

void test_reset_clears_both_directions() {
    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    assert(audio_codec_reset() == AUDIO_CODEC_OK);
    assert(audio_codec_host_encoder_reset_calls() == 1);
    assert(audio_codec_host_decoder_reset_calls() == 1);

    // An encoder reset failure must short-circuit before the decoder reset.
    audio_codec_host_set_encoder_reset_result(ESP_AUDIO_ERR_FAIL);
    assert(audio_codec_reset() == AUDIO_CODEC_ERR_DECODE_FAILED);
    assert(audio_codec_host_decoder_reset_calls() == 1);
}

void test_mutex_contention_is_reported_as_internal() {
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME] = {0};
    uint8_t packet[AUDIO_CODEC_MAX_PACKET_BYTES] = {0};
    audio_codec_packet_t packet_out{};
    audio_codec_pcm_frame_t frame{};

    audio_codec_host_reset();
    assert(audio_codec_init() == ESP_OK);

    audio_codec_host_set_take_mutex_fails(true);
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet_out) ==
        AUDIO_CODEC_ERR_INTERNAL
    );
    assert(
        audio_codec_decode_packet(packet, 32, &frame) ==
        AUDIO_CODEC_ERR_INTERNAL
    );
    assert(audio_codec_reset() == AUDIO_CODEC_ERR_INTERNAL);
}

}  // namespace

int main() {
    // init() has no shutdown counterpart, so every not-ready assertion runs
    // before the one successful init that the runtime tests build on.
    test_mutex_creation_failure_is_reported();
    test_encoder_open_failure_leaves_module_unready();
    test_decoder_open_failure_closes_the_encoder();
    test_init_uses_the_negotiated_profile_and_is_idempotent();
    test_encode_validates_arguments_and_sizes();
    test_encode_maps_engine_errors_and_reports_size();
    test_encode_rejects_an_empty_or_oversized_engine_result();
    test_decode_validates_arguments_and_sizes();
    test_decode_marks_packet_loss_and_requires_a_full_frame();
    test_decode_maps_engine_errors();
    test_reset_clears_both_directions();
    test_mutex_contention_is_reported_as_internal();
    return 0;
}
