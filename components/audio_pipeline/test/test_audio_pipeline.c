#include "audio_pipeline.h"

#include "audio_codec.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

void audio_pipeline_host_reset(void);
void audio_pipeline_host_reset_state(void);
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

static int reference_calls;
static size_t reference_sample_count;
static int16_t reference_first_sample;

static void reference_sink(
    const int16_t *samples,
    size_t sample_count,
    void *context
) {
    (void)context;
    reference_calls++;
    reference_sample_count = sample_count;
    if (samples != NULL && sample_count > 0) {
        reference_first_sample = samples[0];
    }
}

static void prepare(void) {
    audio_pipeline_host_reset();
    audio_pipeline_host_reset_state();
    assert(audio_pipeline_init() == ESP_OK);
    assert(audio_pipeline_is_ready());
    reference_calls = 0;
    reference_sample_count = 0;
    reference_first_sample = 0;
}

static void test_init_requires_codec(void) {
    audio_pipeline_host_reset();
    audio_pipeline_host_reset_state();
    audio_pipeline_host_set_codec_ready(false);
    assert(audio_pipeline_init() == ESP_ERR_INVALID_STATE);
    assert(!audio_pipeline_is_ready());
}

static void test_init_failure_rolls_back_channels(void) {
    audio_pipeline_host_reset();
    audio_pipeline_host_reset_state();
    audio_pipeline_host_set_new_channel_result(ESP_ERR_NO_MEM);
    assert(audio_pipeline_init() == ESP_ERR_NO_MEM);
    assert(!audio_pipeline_is_ready());

    audio_pipeline_host_reset();
    audio_pipeline_host_reset_state();
    audio_pipeline_host_set_rx_init_result(ESP_FAIL);
    assert(audio_pipeline_init() == ESP_FAIL);
    assert(!audio_pipeline_is_ready());
    assert(audio_pipeline_host_del_channel_calls() == 2);

    audio_pipeline_host_reset();
    audio_pipeline_host_reset_state();
    audio_pipeline_host_set_tx_init_result(ESP_FAIL);
    assert(audio_pipeline_init() == ESP_FAIL);
    assert(!audio_pipeline_is_ready());
    assert(audio_pipeline_host_del_channel_calls() == 2);
}

static void test_init_uses_contract_profile_and_is_idempotent(void) {
    prepare();
    assert(audio_pipeline_init() == ESP_OK);
    assert(audio_pipeline_host_new_channel_calls() == 1);

    i2s_std_config_t config;
    memset(&config, 0, sizeof(config));
    assert(audio_pipeline_host_last_config(&config));
    assert(config.clk_cfg.sample_rate_hz == AUDIO_CODEC_SAMPLE_RATE_HZ);
    assert(config.slot_cfg.data_bit_width == I2S_DATA_BIT_WIDTH_16BIT);
    assert(config.slot_cfg.slot_mode == I2S_SLOT_MODE_MONO);
    assert(config.slot_cfg.slot_mask == I2S_STD_SLOT_LEFT);
}

static void test_capture_requires_a_valid_owner(void) {
    prepare();
    audio_codec_pcm_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    assert(
        audio_pipeline_capture_frame(
            AUDIO_PIPELINE_CAPTURE_OWNER_NONE,
            &frame
        ) == AUDIO_CODEC_ERR_NOT_INITIALIZED
    );
    assert(audio_pipeline_host_read_calls() == 0);

    assert(
        audio_pipeline_capture_acquire(
            AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
        ) == ESP_OK
    );
    assert(
        audio_pipeline_capture_acquire(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
        ) == ESP_ERR_INVALID_STATE
    );
    assert(
        audio_pipeline_capture_frame(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT,
            &frame
        ) == AUDIO_CODEC_ERR_NOT_INITIALIZED
    );
    assert(audio_pipeline_host_read_calls() == 0);

    assert(
        audio_pipeline_capture_frame(
            AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE,
            &frame
        ) == AUDIO_CODEC_OK
    );
    assert(frame.pcm_size == AUDIO_CODEC_FRAME_PCM_BYTES);
    assert(audio_pipeline_host_read_calls() == 1);

    assert(
        audio_pipeline_capture_release(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
        ) == ESP_ERR_INVALID_STATE
    );
    assert(
        audio_pipeline_capture_release(
            AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
        ) == ESP_OK
    );
    assert(
        audio_pipeline_capture_get_owner() ==
        AUDIO_PIPELINE_CAPTURE_OWNER_NONE
    );
}

static void test_partial_capture_is_rejected(void) {
    prepare();
    audio_codec_pcm_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    audio_pipeline_host_set_read_bytes(AUDIO_CODEC_FRAME_PCM_BYTES - 2);

    assert(
        audio_pipeline_capture_acquire(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
        ) == ESP_OK
    );
    assert(
        audio_pipeline_capture_frame(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT,
            &frame
        ) == AUDIO_CODEC_ERR_INVALID_PCM_SIZE
    );
    assert(audio_pipeline_get_snapshot().capture_errors == 1);
}

static void test_capture_owner_gate_cannot_disable_an_active_lease(void) {
    prepare();
    assert(
        audio_pipeline_capture_acquire(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
        ) == ESP_OK
    );
    assert(audio_pipeline_set_capturing(false) == ESP_ERR_INVALID_STATE);
    assert(audio_pipeline_is_capturing());
    assert(
        audio_pipeline_capture_get_owner() ==
        AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
    );
    assert(
        audio_pipeline_capture_release(
            AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
        ) == ESP_OK
    );
    assert(!audio_pipeline_is_capturing());
}

static void test_playback_requires_enable_and_full_frame(void) {
    prepare();
    audio_codec_pcm_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;

    assert(
        audio_pipeline_play_frame(&frame) ==
        AUDIO_CODEC_ERR_NOT_INITIALIZED
    );
    assert(audio_pipeline_set_playing(true) == ESP_OK);
    assert(audio_pipeline_is_playing());

    audio_codec_pcm_frame_t partial = frame;
    partial.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES - 2;
    assert(
        audio_pipeline_play_frame(&partial) ==
        AUDIO_CODEC_ERR_INVALID_PCM_SIZE
    );

    assert(audio_pipeline_play_frame(&frame) == AUDIO_CODEC_OK);
    assert(audio_pipeline_get_snapshot().played_frames == 1);
    assert(audio_pipeline_host_write_calls() == 1);

    assert(audio_pipeline_set_playing(false) == ESP_OK);
    assert(!audio_pipeline_is_playing());
    assert(
        audio_pipeline_play_frame(&frame) ==
        AUDIO_CODEC_ERR_NOT_INITIALIZED
    );
}

static void test_playback_error_and_partial_write_are_counted(void) {
    prepare();
    audio_codec_pcm_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    assert(audio_pipeline_set_playing(true) == ESP_OK);

    audio_pipeline_host_set_write_result(ESP_FAIL);
    assert(
        audio_pipeline_play_frame(&frame) ==
        AUDIO_CODEC_ERR_ENCODE_FAILED
    );
    assert(audio_pipeline_get_snapshot().playback_errors == 1);

    audio_pipeline_host_set_write_result(ESP_OK);
    audio_pipeline_host_set_write_bytes(AUDIO_CODEC_FRAME_PCM_BYTES - 2);
    assert(
        audio_pipeline_play_frame(&frame) ==
        AUDIO_CODEC_ERR_ENCODE_FAILED
    );
    assert(audio_pipeline_get_snapshot().playback_errors == 2);
}

static void test_reference_sink_receives_the_played_frame(void) {
    prepare();
    assert(audio_pipeline_set_reference_sink(reference_sink, NULL) == ESP_OK);

    audio_codec_pcm_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    frame.pcm[0] = 321;
    assert(audio_pipeline_set_playing(true) == ESP_OK);
    assert(audio_pipeline_play_frame(&frame) == AUDIO_CODEC_OK);

    assert(reference_calls == 1);
    assert(reference_sample_count == AUDIO_CODEC_SAMPLES_PER_FRAME);
    assert(reference_first_sample == 321);

    assert(audio_pipeline_set_reference_sink(NULL, (void *)0x1234) == ESP_OK);
    assert(audio_pipeline_play_frame(&frame) == AUDIO_CODEC_OK);
    assert(reference_calls == 1);
}

static void test_state_snapshot_tracks_capture_and_playback(void) {
    prepare();
    audio_pipeline_snapshot_t snapshot = audio_pipeline_get_snapshot();
    assert(snapshot.state == AUDIO_PIPELINE_STATE_STOPPED);
    assert(snapshot.is_microphone_enabled);
    assert(snapshot.is_speaker_enabled);

    assert(
        audio_pipeline_capture_acquire(
            AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
        ) == ESP_OK
    );
    snapshot = audio_pipeline_get_snapshot();
    assert(snapshot.state == AUDIO_PIPELINE_STATE_CAPTURING);

    assert(
        audio_pipeline_capture_release(
            AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE
        ) == ESP_OK
    );
    assert(audio_pipeline_set_playing(true) == ESP_OK);
    snapshot = audio_pipeline_get_snapshot();
    assert(snapshot.state == AUDIO_PIPELINE_STATE_PLAYING);
    assert(audio_pipeline_set_playing(false) == ESP_OK);
    assert(
        audio_pipeline_get_snapshot().state ==
        AUDIO_PIPELINE_STATE_STOPPED
    );
}

static void test_module_descriptor_is_stable(void) {
    const module_descriptor_t *value = audio_pipeline_module_descriptor();
    assert(value != NULL);
    assert(strcmp(value->module_name, "audio_pipeline") == 0);
    assert(strcmp(value->version, "1.0.0") == 0);
    assert(value->initialize != NULL);
    assert(value->shutdown == NULL);
}

int main(void) {
    test_init_requires_codec();
    test_init_failure_rolls_back_channels();
    test_init_uses_contract_profile_and_is_idempotent();
    test_capture_requires_a_valid_owner();
    test_partial_capture_is_rejected();
    test_capture_owner_gate_cannot_disable_an_active_lease();
    test_playback_requires_enable_and_full_frame();
    test_playback_error_and_partial_write_are_counted();
    test_reference_sink_receives_the_played_frame();
    test_state_snapshot_tracks_capture_and_playback();
    test_module_descriptor_is_stable();
    return 0;
}
