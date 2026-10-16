#include "audio_codec.h"

#include <cassert>
#include <cstring>

namespace {

void test_negotiated_profile_is_frozen() {
    assert(std::strcmp(AUDIO_CODEC_SCHEMA_VERSION, "1.0.0") == 0);
    assert(std::strcmp(AUDIO_CODEC_ENCODING_NAME, "opus") == 0);
    assert(AUDIO_CODEC_SAMPLE_RATE_HZ == 16000);
    assert(AUDIO_CODEC_CHANNEL_COUNT == 1);
    assert(AUDIO_CODEC_FRAME_DURATION_MS == 20);

    // 16 kHz * 20 ms => 320 samples per channel, 640 bytes of 16-bit mono PCM.
    assert(AUDIO_CODEC_SAMPLES_PER_FRAME == 320);
    assert(AUDIO_CODEC_FRAME_PCM_BYTES == 640);
    assert(
        sizeof(((audio_codec_pcm_frame_t *)nullptr)->pcm) ==
        AUDIO_CODEC_FRAME_PCM_BYTES
    );
    assert(AUDIO_CODEC_MAX_PACKET_BYTES == 1024);
    assert(
        sizeof(((audio_codec_packet_t *)nullptr)->packet) ==
        AUDIO_CODEC_MAX_PACKET_BYTES
    );
}

void test_error_names_are_stable() {
    assert(std::strcmp(audio_codec_error_name(AUDIO_CODEC_OK), "ok") == 0);
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_NOT_INITIALIZED),
            "audio_codec_not_initialized"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_INVALID_ARGUMENT),
            "audio_codec_invalid_argument"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_INVALID_PACKET_SIZE),
            "audio_codec_invalid_packet_size"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_INVALID_PCM_SIZE),
            "audio_codec_invalid_pcm_size"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_ENCODE_FAILED),
            "audio_codec_encode_failed"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_DECODE_FAILED),
            "audio_codec_decode_failed"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL),
            "audio_codec_output_too_small"
        ) == 0
    );
    assert(
        std::strcmp(
            audio_codec_error_name(AUDIO_CODEC_ERR_INTERNAL),
            "audio_codec_internal"
        ) == 0
    );
    // An out-of-contract value must not leak an unstable string.
    assert(
        std::strcmp(
            audio_codec_error_name(static_cast<audio_codec_error_t>(99)),
            "audio_codec_internal"
        ) == 0
    );
}

void test_calls_before_init_are_refused() {
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME] = {0};
    audio_codec_packet_t packet{};
    audio_codec_pcm_frame_t frame{};

    assert(!audio_codec_is_ready());
    assert(
        audio_codec_encode_frame(pcm, AUDIO_CODEC_SAMPLES_PER_FRAME, &packet) ==
        AUDIO_CODEC_ERR_NOT_INITIALIZED
    );
    assert(
        audio_codec_decode_packet(packet.packet, 0, &frame) ==
        AUDIO_CODEC_ERR_NOT_INITIALIZED
    );
    assert(audio_codec_reset() == AUDIO_CODEC_ERR_NOT_INITIALIZED);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor = audio_codec_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "audio_codec") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_negotiated_profile_is_frozen();
    test_error_names_are_stable();
    test_calls_before_init_are_refused();
    test_module_descriptor_is_stable();
    return 0;
}
