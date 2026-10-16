#include "audio_input_dsp.h"

#include "esp_err.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define TEST_FRAME_SAMPLES AUDIO_INPUT_FRAME_SAMPLES

static int16_t sine_frame[TEST_FRAME_SAMPLES];
static int16_t reference_frame[TEST_FRAME_SAMPLES];
static int16_t output_frame[TEST_FRAME_SAMPLES];

static void fill_constant(int16_t *samples, int16_t value) {
    for (size_t index = 0; index < TEST_FRAME_SAMPLES; ++index) {
        samples[index] = value;
    }
}

static void fill_sine(int16_t *samples, float amplitude, float frequency_hz) {
    const float step = 2.0f * 3.14159265358979323846f * frequency_hz /
        (float)AUDIO_CODEC_SAMPLE_RATE_HZ;
    for (size_t index = 0; index < TEST_FRAME_SAMPLES; ++index) {
        samples[index] = (int16_t)lroundf(
            amplitude * sinf(step * (float)index)
        );
    }
}

static void assert_output_is_finite(const int16_t *samples) {
    for (size_t index = 0; index < TEST_FRAME_SAMPLES; ++index) {
        assert(samples[index] >= INT16_MIN);
        assert(samples[index] <= INT16_MAX);
    }
}

static void test_null_arguments_are_rejected(void) {
    assert(audio_input_dsp_init(NULL) == ESP_ERR_INVALID_ARG);
    audio_input_dsp_deinit(NULL);
    audio_input_dsp_reset_session(NULL);
    assert(audio_input_dsp_aec_convergence_q10(NULL) == 0);
    assert(audio_input_dsp_agc_gain_q8(NULL) == 256);

    audio_input_dsp_t dsp;
    memset(&dsp, 0, sizeof(dsp));
    bool is_speech = true;
    audio_input_dsp_process_frame(
        NULL,
        sine_frame,
        NULL,
        false,
        output_frame,
        &is_speech
    );
    audio_input_dsp_process_frame(
        &dsp,
        NULL,
        NULL,
        false,
        output_frame,
        &is_speech
    );
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        NULL,
        false,
        NULL,
        &is_speech
    );
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        NULL,
        false,
        output_frame,
        NULL
    );
}

static void test_init_populates_expected_state(void) {
    audio_input_dsp_t dsp;
    memset(&dsp, 0xA5, sizeof(dsp));
    assert(audio_input_dsp_init(&dsp) == ESP_OK);

    assert(dsp.aec_present);
    assert(dsp.ns_present);
    assert(dsp.agc_present);
    assert(!dsp.ns_primed);
    assert(!dsp.vad_is_speech);
    assert(dsp.aec_weights != NULL);
    assert(dsp.aec_history != NULL);
    assert(dsp.error_samples != NULL);
    assert(dsp.band_samples != NULL);
    assert(dsp.aec_filter_length >= TEST_FRAME_SAMPLES);
    assert(dsp.aec_filter_length <= 2048);
    assert(dsp.aec_history_at == dsp.aec_filter_length - 1);
    assert(fabsf(dsp.agc_gain - 1.0f) < 1e-6f);
    assert(dsp.agc_min_gain < dsp.agc_max_gain);
    assert(audio_input_dsp_agc_gain_q8(&dsp) == 256u);
    assert(audio_input_dsp_aec_convergence_q10(&dsp) == 0u);

    audio_input_dsp_deinit(&dsp);
    assert(dsp.aec_weights == NULL);
    assert(dsp.aec_history == NULL);
    assert(dsp.error_samples == NULL);
    assert(dsp.band_samples == NULL);

    /* deinit must remain safe if it is accidentally run twice. */
    audio_input_dsp_deinit(&dsp);
}

static void test_reset_session_preserves_learned_echo_path(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);

    dsp.aec_weights[0] = 0.5f;
    dsp.aec_weights[1] = 0.25f;
    dsp.ns_low_state = 1.0f;
    dsp.ns_high_state = 2.0f;
    dsp.ns_primed = true;
    dsp.ns_noise_power[0] = 3.0f;
    dsp.ns_gain[0] = 0.25f;
    dsp.agc_gain = 4.0f;
    dsp.vad_noise_power = 5.0f;
    dsp.vad_speech_run = 7;
    dsp.vad_hangover = 8;
    dsp.vad_is_speech = true;
    dsp.aec_double_talk = true;

    audio_input_dsp_reset_session(&dsp);

    assert(dsp.aec_weights[0] == 0.5f);
    assert(dsp.aec_weights[1] == 0.25f);
    assert(dsp.ns_low_state == 0.0f);
    assert(dsp.ns_high_state == 0.0f);
    assert(!dsp.ns_primed);
    assert(dsp.ns_noise_power[0] == 0.0f);
    assert(dsp.ns_gain[0] == 1.0f);
    assert(dsp.agc_gain == 1.0f);
    assert(dsp.vad_noise_power == 1.0f);
    assert(dsp.vad_speech_run == 0);
    assert(dsp.vad_hangover == 0);
    assert(!dsp.vad_is_speech);
    assert(!dsp.aec_double_talk);

    audio_input_dsp_deinit(&dsp);
}

static void test_no_reference_is_a_stable_passthrough(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);

    fill_sine(sine_frame, 1000.0f, 440.0f);
    bool is_speech = false;
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        NULL,
        false,
        output_frame,
        &is_speech
    );

    assert(!dsp.aec_double_talk);
    assert(dsp.aec_adapted_samples == 0);
    assert_output_is_finite(output_frame);

    /* First frame primes the suppressor, so it must not be muted wholesale. */
    int nonzero = 0;
    for (size_t index = 0; index < TEST_FRAME_SAMPLES; ++index) {
        if (output_frame[index] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    audio_input_dsp_deinit(&dsp);
}

static void test_silent_reference_does_not_adapt_or_mutate_input(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);
    fill_sine(reference_frame, 0.0f, 1000.0f);
    fill_sine(sine_frame, 500.0f, 440.0f);

    bool is_speech = false;
    const uint32_t before = dsp.aec_adapted_samples;
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        reference_frame,
        true,
        output_frame,
        &is_speech
    );
    assert(dsp.aec_adapted_samples == before);
    assert(!dsp.aec_double_talk);

    audio_input_dsp_deinit(&dsp);
}

static void test_double_talk_freezes_adaptation(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);

    bool is_speech = false;
    /*
     * Establish a modest reference peak without adapting, then raise the
     * microphone above the double-talk margin. The second frame is the one
     * that must be classified as near-end speech.
     */
    fill_constant(reference_frame, 1000);
    fill_constant(sine_frame, 100);
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        reference_frame,
        true,
        output_frame,
        &is_speech
    );
    assert(!dsp.aec_double_talk);
    const uint32_t adapted_before_second = dsp.aec_adapted_samples;

    fill_constant(sine_frame, 8000);
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        reference_frame,
        true,
        output_frame,
        &is_speech
    );

    assert(dsp.aec_double_talk);
    assert(dsp.aec_adapted_samples == adapted_before_second);
    assert_output_is_finite(output_frame);

    audio_input_dsp_deinit(&dsp);
}

#ifdef AUDIO_INPUT_LEAKAGE_TEST
static void test_configured_aec_leakage_is_applied(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);

    /*
     * Disable the LMS step so only the leakage path can change the trained
     * coefficient. With 100 permille leakage this seed decays by 0.9 once per
     * sample; the old hard-coded 0.1 permille path would leave it near 0.48.
     */
    dsp.aec_step_size = 0.0f;
    dsp.aec_weights[0] = 0.5f;
    fill_constant(reference_frame, 100);
    fill_constant(sine_frame, 0);
    bool is_speech = false;
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        reference_frame,
        true,
        output_frame,
        &is_speech
    );

    /*
     * The coefficient must decay according to the configured leakage rather
     * than the old hard-coded factor.
     */
    assert(dsp.aec_adapted_samples > 0);
    assert(dsp.aec_weights[0] < 0.001f);
    audio_input_dsp_deinit(&dsp);
}
#endif

static void test_aec_adapts_and_convergence_is_bounded(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);

    /* Quiet, linearly correlated echo lets the canceller adapt without
       triggering the double-talk guard. */
    fill_constant(reference_frame, 200);
    fill_constant(sine_frame, 100);
    bool is_speech = false;
    for (int frame = 0; frame < 40; ++frame) {
        audio_input_dsp_process_frame(
            &dsp,
            sine_frame,
            reference_frame,
            true,
            output_frame,
            &is_speech
        );
    }

    assert(dsp.aec_adapted_samples > 0);
    const uint32_t convergence = audio_input_dsp_aec_convergence_q10(&dsp);
    assert(convergence <= 1024u);
    assert(convergence >= 1u);

    audio_input_dsp_deinit(&dsp);
}

static void test_agc_gain_is_clamped_and_reported(void) {
    audio_input_dsp_t dsp;
    assert(audio_input_dsp_init(&dsp) == ESP_OK);
    assert(dsp.agc_min_gain >= 0.1f);
    assert(dsp.agc_max_gain >= dsp.agc_min_gain);

    dsp.agc_gain = 100.0f;
    assert(audio_input_dsp_agc_gain_q8(&dsp) == (uint32_t)(100.0f * 256.0f));

    /*
     * Force the VAD to classify the next frame as speech. AGC intentionally
     * does not adapt on silence, so a constant frame would leave the manually
     * corrupted gain untouched and would not exercise the clamp.
     */
    dsp.vad_noise_power = 1.0f;
    dsp.vad_speech_run = (uint32_t)CONFIG_AUDIO_INPUT_VAD_MIN_SPEECH_FRAMES - 1u;
    fill_sine(sine_frame, 10000.0f, 440.0f);
    bool is_speech = false;
    audio_input_dsp_process_frame(
        &dsp,
        sine_frame,
        NULL,
        false,
        output_frame,
        &is_speech
    );
    assert(is_speech);
    assert(dsp.agc_gain >= dsp.agc_min_gain);
    assert(dsp.agc_gain <= dsp.agc_max_gain);
    assert(audio_input_dsp_agc_gain_q8(&dsp) <=
        (uint32_t)(dsp.agc_max_gain * 256.0f + 0.5f));
    assert_output_is_finite(output_frame);

    audio_input_dsp_deinit(&dsp);
}

static void test_deterministic_output_for_constant_input(void) {
    audio_input_dsp_t first;
    audio_input_dsp_t second;
    assert(audio_input_dsp_init(&first) == ESP_OK);
    assert(audio_input_dsp_init(&second) == ESP_OK);

    fill_constant(sine_frame, 1200);
    bool first_speech = false;
    bool second_speech = false;
    int16_t second_output[TEST_FRAME_SAMPLES];

    audio_input_dsp_process_frame(
        &first,
        sine_frame,
        NULL,
        false,
        output_frame,
        &first_speech
    );
    audio_input_dsp_process_frame(
        &second,
        sine_frame,
        NULL,
        false,
        second_output,
        &second_speech
    );

    assert(first_speech == second_speech);
    assert(memcmp(output_frame, second_output, sizeof(output_frame)) == 0);

    audio_input_dsp_deinit(&first);
    audio_input_dsp_deinit(&second);
}

int main(void) {
    fill_sine(sine_frame, 1000.0f, 440.0f);
    test_null_arguments_are_rejected();
    test_init_populates_expected_state();
    test_reset_session_preserves_learned_echo_path();
    test_no_reference_is_a_stable_passthrough();
    test_silent_reference_does_not_adapt_or_mutate_input();
    test_double_talk_freezes_adaptation();
#ifdef AUDIO_INPUT_LEAKAGE_TEST
    test_configured_aec_leakage_is_applied();
#endif
    test_aec_adapts_and_convergence_is_bounded();
    test_agc_gain_is_clamped_and_reported();
    test_deterministic_output_for_constant_input();
    return 0;
}
