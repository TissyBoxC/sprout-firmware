#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_codec.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Samples per captured frame; the contract fixes 16 kHz stereo mono. */
#define AUDIO_INPUT_FRAME_SAMPLES AUDIO_CODEC_SAMPLES_PER_FRAME

/** @brief Analysis bands used by the sub-band noise suppressor. */
#define AUDIO_INPUT_BAND_COUNT 3

/**
 * @brief Adaptive capture processor state for one device.
 *
 * A single instance is owned by the audio_input module and is only touched by
 * the capture task, so it needs no internal locking. The scratch buffers are
 * allocated once during initialization to keep the capture task stack bounded.
 */
typedef struct {
    size_t aec_filter_length;
    float *aec_weights;
    float *aec_history;
    size_t aec_history_at;
    float aec_step_size;
    float aec_leakage;
    float aec_double_talk_factor;
    float aec_reference_peak;
    float aec_residual;
    uint32_t aec_adapted_samples;
    bool aec_present;
    bool aec_double_talk;

    float ns_low_state;
    float ns_high_state;
    float ns_noise_power[AUDIO_INPUT_BAND_COUNT];
    float ns_gain[AUDIO_INPUT_BAND_COUNT];
    bool ns_primed;
    bool ns_present;

    float agc_gain;
    float agc_target_rms;
    float agc_min_gain;
    float agc_max_gain;
    float agc_attack;
    float agc_release;
    bool agc_present;

    float vad_noise_power;
    uint32_t vad_speech_run;
    uint32_t vad_hangover;
    bool vad_is_speech;

    // Scratch buffers reused every frame. They live on the heap so the capture
    // task stack only needs room for its call frames.
    float *error_samples;
    float *band_samples;
} audio_input_dsp_t;

/** @brief Allocate DSP state and validate every configured coefficient. */
esp_err_t audio_input_dsp_init(audio_input_dsp_t *dsp);

/** @brief Free every DSP allocation; safe on a partially initialized state. */
void audio_input_dsp_deinit(audio_input_dsp_t *dsp);

/** @brief Reset session-varying state while keeping the learned echo path. */
void audio_input_dsp_reset_session(audio_input_dsp_t *dsp);

/**
 * @brief Process one captured frame into a cleaned, calibrated frame.
 *
 * Runs echo cancellation, noise suppression, voice activity detection, then
 * gain calibration. reference may be NULL when the speaker is silent.
 */
void audio_input_dsp_process_frame(
    audio_input_dsp_t *dsp,
    const int16_t *microphone,
    const int16_t *reference,
    bool has_reference,
    int16_t *output,
    bool *is_speech
);

/** @brief Report echo canceller convergence on a 0..1024 scale. */
uint32_t audio_input_dsp_aec_convergence_q10(const audio_input_dsp_t *dsp);

/** @brief Report the current gain calibration on a Q8 scale. */
uint32_t audio_input_dsp_agc_gain_q8(const audio_input_dsp_t *dsp);

#ifdef __cplusplus
}
#endif
