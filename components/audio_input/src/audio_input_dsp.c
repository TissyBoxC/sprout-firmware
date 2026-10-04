#include "audio_input_dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *const TAG = "audio_input_dsp";

#define AUDIO_INPUT_EPSILON 1e-9f
#define AUDIO_INPUT_INT16_MAX 32767.0f
#define AUDIO_INPUT_INT16_MIN (-32768.0f)

// The sub-band suppressor splits the signal with one-pole low passes and keeps
// the high band as the exact remainder, so the bands reconstruct the input
// without an FFT or overlap-add buffer. These cutoffs isolate rumble, the
// vowel band, and the consonant band where noise usually dominates.
#define AUDIO_INPUT_LOW_SPLIT_HZ 300.0f
#define AUDIO_INPUT_HIGH_SPLIT_HZ 2000.0f

#define AUDIO_INPUT_PI 3.14159265358979323846f

static float audio_input_clamp(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int16_t audio_input_saturate(float value) {
    if (value > AUDIO_INPUT_INT16_MAX) {
        return (int16_t)AUDIO_INPUT_INT16_MAX;
    }
    if (value < AUDIO_INPUT_INT16_MIN) {
        return (int16_t)AUDIO_INPUT_INT16_MIN;
    }
    return (int16_t)value;
}

static float audio_input_one_pole_alpha(float cutoff_hz) {
    const float sample_rate = (float)AUDIO_CODEC_SAMPLE_RATE_HZ;
    return 1.0f - expf(-2.0f * AUDIO_INPUT_PI * cutoff_hz / sample_rate);
}

esp_err_t audio_input_dsp_init(audio_input_dsp_t *dsp) {
    if (dsp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(dsp, 0, sizeof(*dsp));
    dsp->agc_gain = 1.0f;
    dsp->aec_residual = 1.0f;
    dsp->vad_noise_power = 1.0f;
    for (int band = 0; band < AUDIO_INPUT_BAND_COUNT; ++band) {
        dsp->ns_gain[band] = 1.0f;
    }

    dsp->aec_present = true;
    dsp->aec_filter_length = (size_t)CONFIG_AUDIO_INPUT_AEC_FILTER_TAPS;
    if (dsp->aec_filter_length < AUDIO_INPUT_FRAME_SAMPLES ||
        dsp->aec_filter_length > 2048) {
        return ESP_ERR_INVALID_ARG;
    }
    dsp->aec_weights = calloc(dsp->aec_filter_length, sizeof(float));
    dsp->aec_history = calloc(dsp->aec_filter_length, sizeof(float));
    dsp->aec_history_at = dsp->aec_filter_length - 1;
    dsp->error_samples =
        calloc(AUDIO_INPUT_FRAME_SAMPLES, sizeof(float));
    dsp->band_samples = calloc(
        (size_t)AUDIO_INPUT_BAND_COUNT * AUDIO_INPUT_FRAME_SAMPLES,
        sizeof(float)
    );
    if (dsp->aec_weights == NULL || dsp->aec_history == NULL ||
        dsp->error_samples == NULL || dsp->band_samples == NULL) {
        audio_input_dsp_deinit(dsp);
        return ESP_ERR_NO_MEM;
    }

    dsp->aec_step_size =
        (float)CONFIG_AUDIO_INPUT_AEC_STEP_PERMILLE / 1000.0f;
    dsp->aec_leakage =
        (float)CONFIG_AUDIO_INPUT_AEC_LEAKAGE_PERMILLE / 1000.0f;
    dsp->aec_double_talk_factor =
        (float)CONFIG_AUDIO_INPUT_AEC_DOUBLE_TALK_FACTOR_MILLI / 1000.0f;

    dsp->ns_present = true;
    dsp->agc_present = true;
    dsp->agc_target_rms = (float)CONFIG_AUDIO_INPUT_AGC_TARGET_RMS;
    dsp->agc_min_gain =
        (float)CONFIG_AUDIO_INPUT_AGC_MIN_GAIN_PERCENT / 100.0f;
    dsp->agc_max_gain =
        (float)CONFIG_AUDIO_INPUT_AGC_MAX_GAIN_PERCENT / 100.0f;
    if (dsp->agc_min_gain > dsp->agc_max_gain) {
        // A misconfigured range would make the gain wander; reject it instead
        // of silently clamping every frame.
        audio_input_dsp_deinit(dsp);
        return ESP_ERR_INVALID_ARG;
    }
    dsp->agc_attack =
        (float)CONFIG_AUDIO_INPUT_AGC_ATTACK_PERMILLE / 1000.0f;
    dsp->agc_release =
        (float)CONFIG_AUDIO_INPUT_AGC_RELEASE_PERMILLE / 1000.0f;

    ESP_LOGI(
        TAG,
        "capture DSP ready: filter=%u taps, ns=%d bands, agc=%.2f..%.2f",
        (unsigned int)dsp->aec_filter_length,
        AUDIO_INPUT_BAND_COUNT,
        (double)dsp->agc_min_gain,
        (double)dsp->agc_max_gain
    );
    return ESP_OK;
}

void audio_input_dsp_deinit(audio_input_dsp_t *dsp) {
    if (dsp == NULL) {
        return;
    }
    free(dsp->aec_weights);
    free(dsp->aec_history);
    free(dsp->error_samples);
    free(dsp->band_samples);
    dsp->aec_weights = NULL;
    dsp->aec_history = NULL;
    dsp->error_samples = NULL;
    dsp->band_samples = NULL;
}

void audio_input_dsp_reset_session(audio_input_dsp_t *dsp) {
    if (dsp == NULL) {
        return;
    }
    // The learned echo path is kept across conversations because the device
    // usually stays in the same room, but the noise, gain, and speech state
    // must start clean so the first utterance is not judged against stale data.
    dsp->ns_low_state = 0.0f;
    dsp->ns_high_state = 0.0f;
    dsp->ns_primed = false;
    for (int band = 0; band < AUDIO_INPUT_BAND_COUNT; ++band) {
        dsp->ns_noise_power[band] = 0.0f;
        dsp->ns_gain[band] = 1.0f;
    }
    dsp->agc_gain = 1.0f;
    dsp->vad_noise_power = 1.0f;
    dsp->vad_speech_run = 0;
    dsp->vad_hangover = 0;
    dsp->vad_is_speech = false;
    dsp->aec_double_talk = false;
}

static void audio_input_aec_process(
    audio_input_dsp_t *dsp,
    const int16_t *microphone,
    const int16_t *reference,
    bool has_reference,
    int16_t *output
) {
    dsp->aec_double_talk = false;
    if (!dsp->aec_present || !has_reference || reference == NULL) {
        memcpy(output, microphone, sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES);
        return;
    }

    const size_t filter_length = dsp->aec_filter_length;
    float reference_power = 0.0f;
    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        const float sample = (float)reference[index];
        reference_power += sample * sample;
    }
    if (reference_power < AUDIO_INPUT_EPSILON) {
        // Silence carries no echo, so pass the microphone through and keep the
        // learned filter intact.
        memcpy(output, microphone, sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES);
        return;
    }

    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        const float reference_sample = (float)reference[index];
        dsp->aec_history_at = (dsp->aec_history_at + 1) % filter_length;
        dsp->aec_history[dsp->aec_history_at] = reference_sample;

        const float magnitude = fabsf(reference_sample);
        if (magnitude > dsp->aec_reference_peak) {
            dsp->aec_reference_peak = magnitude;
        } else {
            // The peak decays to about a 40 ms memory, matching the short echo
            // path of a tabletop speaker and microphone.
            dsp->aec_reference_peak *= 0.998f;
        }

        float estimate = 0.0f;
        float energy = AUDIO_INPUT_EPSILON;
        size_t tap = dsp->aec_history_at;
        for (size_t coefficient = 0; coefficient < filter_length; ++coefficient) {
            const float history_sample = dsp->aec_history[tap];
            estimate += dsp->aec_weights[coefficient] * history_sample;
            energy += history_sample * history_sample;
            tap = (tap == 0) ? filter_length - 1 : tap - 1;
        }

        const float microphone_sample = (float)microphone[index];
        const float error_sample = microphone_sample - estimate;
        dsp->error_samples[index] = error_sample;
        output[index] = audio_input_saturate(error_sample);
        dsp->aec_residual =
            0.9999f * dsp->aec_residual + 0.0001f * error_sample * error_sample;

        // Near-end speech that rises above the recent echo peak means someone
        // is talking over the device; freezing the update keeps that voice from
        // being learned as echo and subtracted from the real signal.
        if (dsp->aec_reference_peak > 1.0f &&
            fabsf(microphone_sample) >
                dsp->aec_double_talk_factor * dsp->aec_reference_peak) {
            dsp->aec_double_talk = true;
            continue;
        }

        const float delta = dsp->aec_step_size * error_sample / energy;
        tap = dsp->aec_history_at;
        for (size_t coefficient = 0; coefficient < filter_length; ++coefficient) {
            float weight = dsp->aec_weights[coefficient] +
                delta * dsp->aec_history[tap];
            // Leakage keeps the filter from drifting during long silence.
            dsp->aec_weights[coefficient] = weight * 0.9999f;
            tap = (tap == 0) ? filter_length - 1 : tap - 1;
        }
        dsp->aec_adapted_samples++;
    }
}

static void audio_input_ns_process(
    audio_input_dsp_t *dsp,
    const int16_t *input,
    int16_t *output
) {
    if (!dsp->ns_present) {
        memcpy(output, input, sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES);
        return;
    }

    const float low_alpha = audio_input_one_pole_alpha(AUDIO_INPUT_LOW_SPLIT_HZ);
    const float high_alpha = audio_input_one_pole_alpha(AUDIO_INPUT_HIGH_SPLIT_HZ);
    float energy[AUDIO_INPUT_BAND_COUNT] = {0.0f, 0.0f, 0.0f};

    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        const float sample = (float)input[index];
        dsp->ns_low_state += low_alpha * (sample - dsp->ns_low_state);
        const float low = dsp->ns_low_state;
        const float mid_high = sample - low;
        dsp->ns_high_state += high_alpha * (mid_high - dsp->ns_high_state);
        const float mid = dsp->ns_high_state;
        const float high = mid_high - mid;

        dsp->band_samples[0 * AUDIO_INPUT_FRAME_SAMPLES + index] = low;
        dsp->band_samples[1 * AUDIO_INPUT_FRAME_SAMPLES + index] = mid;
        dsp->band_samples[2 * AUDIO_INPUT_FRAME_SAMPLES + index] = high;
        energy[0] += low * low;
        energy[1] += mid * mid;
        energy[2] += high * high;
    }

    if (!dsp->ns_primed) {
        // Treat the first frame as noise so the suppressor does not over-attenuate
        // before it has observed any signal.
        for (int band = 0; band < AUDIO_INPUT_BAND_COUNT; ++band) {
            dsp->ns_noise_power[band] =
                energy[band] / (float)AUDIO_INPUT_FRAME_SAMPLES;
            dsp->ns_gain[band] = 1.0f;
        }
        dsp->ns_primed = true;
        memcpy(output, input, sizeof(int16_t) * AUDIO_INPUT_FRAME_SAMPLES);
        return;
    }

    const float downward =
        (float)CONFIG_AUDIO_INPUT_NS_NOISE_DOWNWARD_PERMILLE / 1000.0f;
    const float upward =
        (float)CONFIG_AUDIO_INPUT_NS_NOISE_UPWARD_PERMILLE / 1000.0f;
    const float smoothing =
        (float)CONFIG_AUDIO_INPUT_NS_GAIN_SMOOTHING_PERMILLE / 1000.0f;
    const float floor_gain =
        (float)CONFIG_AUDIO_INPUT_NS_FLOOR_GAIN_PERCENT / 100.0f;

    for (int band = 0; band < AUDIO_INPUT_BAND_COUNT; ++band) {
        const float observed = energy[band] / (float)AUDIO_INPUT_FRAME_SAMPLES;
        float noise = dsp->ns_noise_power[band];
        // A sudden loud burst is far more likely to be speech than a new noise
        // source, so the floor drops quickly but rises slowly. Letting it climb
        // on speech would erase the voice along with the noise.
        if (observed < noise) {
            noise = downward * noise + (1.0f - downward) * observed;
        } else {
            noise = upward * noise + (1.0f - upward) * observed;
        }
        dsp->ns_noise_power[band] = noise;

        float target = 1.0f;
        if (noise > AUDIO_INPUT_EPSILON) {
            const float snr = fmaxf(observed - noise, 0.0f) / noise;
            target = snr / (1.0f + snr);
            if (target < floor_gain) {
                target = floor_gain;
            }
        }
        dsp->ns_gain[band] =
            smoothing * dsp->ns_gain[band] + (1.0f - smoothing) * target;
    }

    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        const float cleaned =
            dsp->ns_gain[0] * dsp->band_samples[0 * AUDIO_INPUT_FRAME_SAMPLES + index] +
            dsp->ns_gain[1] * dsp->band_samples[1 * AUDIO_INPUT_FRAME_SAMPLES + index] +
            dsp->ns_gain[2] * dsp->band_samples[2 * AUDIO_INPUT_FRAME_SAMPLES + index];
        output[index] = audio_input_saturate(cleaned);
    }
}

static bool audio_input_vad_process(
    audio_input_dsp_t *dsp,
    const int16_t *samples
) {
    float sum_squares = 0.0f;
    uint32_t zero_crossings = 0;
    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        const float sample = (float)samples[index];
        sum_squares += sample * sample;
        if (index > 0 &&
            ((samples[index] >= 0) != (samples[index - 1] >= 0))) {
            zero_crossings++;
        }
    }
    const float mean_power =
        sum_squares / (float)AUDIO_INPUT_FRAME_SAMPLES + AUDIO_INPUT_EPSILON;
    const float zero_crossing_rate =
        (float)zero_crossings / (float)(AUDIO_INPUT_FRAME_SAMPLES - 1);

    const float downward =
        (float)CONFIG_AUDIO_INPUT_VAD_NOISE_DOWNWARD_PERMILLE / 1000.0f;
    const float upward =
        (float)CONFIG_AUDIO_INPUT_VAD_NOISE_UPWARD_PERMILLE / 1000.0f;
    float noise = dsp->vad_noise_power;
    if (mean_power < noise) {
        noise = downward * noise + (1.0f - downward) * mean_power;
    } else {
        noise = upward * noise + (1.0f - upward) * mean_power;
    }
    dsp->vad_noise_power = noise;

    const float threshold =
        (float)CONFIG_AUDIO_INPUT_VAD_SNR_THRESHOLD_MILLI / 1000.0f;
    const float snr = mean_power / fmaxf(noise, AUDIO_INPUT_EPSILON);
    // Voiced speech crosses zero steadily but not at the rate of broadband
    // hiss, so the zero-crossing window rejects clicks and air noise.
    const bool candidate = snr > threshold &&
        zero_crossing_rate >= 0.01f && zero_crossing_rate <= 0.45f;

    if (candidate) {
        dsp->vad_speech_run++;
    } else {
        dsp->vad_speech_run = 0;
    }

    if (dsp->vad_speech_run >=
        (uint32_t)CONFIG_AUDIO_INPUT_VAD_MIN_SPEECH_FRAMES) {
        dsp->vad_hangover = (uint32_t)CONFIG_AUDIO_INPUT_VAD_HANGOVER_FRAMES;
        dsp->vad_is_speech = true;
    } else if (dsp->vad_hangover > 0) {
        dsp->vad_hangover--;
        dsp->vad_is_speech = true;
    } else {
        dsp->vad_is_speech = false;
    }
    return dsp->vad_is_speech;
}

static void audio_input_agc_process(
    audio_input_dsp_t *dsp,
    int16_t *samples,
    bool allow_adapt
) {
    float sum_squares = 0.0f;
    float peak = 0.0f;
    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        const float sample = (float)samples[index];
        sum_squares += sample * sample;
        const float magnitude = fabsf(sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    if (allow_adapt && dsp->agc_present) {
        const float rms =
            sqrtf(sum_squares / (float)AUDIO_INPUT_FRAME_SAMPLES);
        float desired = dsp->agc_target_rms / fmaxf(rms, 1.0f);
        if (peak > 1.0f) {
            // Back off so a sudden loud syllable cannot clip, and keep applying
            // the largest safe gain to that frame.
            const float clipping_limit = AUDIO_INPUT_INT16_MAX / peak;
            if (desired > clipping_limit) {
                desired = clipping_limit;
            }
        }
        desired = audio_input_clamp(
            desired,
            dsp->agc_min_gain,
            dsp->agc_max_gain
        );
        // Attack corrects an over-loud frame quickly; release restores gain
        // slowly so a quiet pause does not lift the noise into the next word.
        const float rate =
            desired < dsp->agc_gain ? dsp->agc_attack : dsp->agc_release;
        dsp->agc_gain = rate * dsp->agc_gain + (1.0f - rate) * desired;
        dsp->agc_gain = audio_input_clamp(
            dsp->agc_gain,
            dsp->agc_min_gain,
            dsp->agc_max_gain
        );
    }

    if (!dsp->agc_present) {
        return;
    }
    for (size_t index = 0; index < AUDIO_INPUT_FRAME_SAMPLES; ++index) {
        samples[index] = audio_input_saturate(
            (float)samples[index] * dsp->agc_gain
        );
    }
}

void audio_input_dsp_process_frame(
    audio_input_dsp_t *dsp,
    const int16_t *microphone,
    const int16_t *reference,
    bool has_reference,
    int16_t *output,
    bool *is_speech
) {
    if (dsp == NULL || microphone == NULL || output == NULL || is_speech == NULL) {
        return;
    }

    int16_t aec_output[AUDIO_INPUT_FRAME_SAMPLES];
    audio_input_aec_process(
        dsp,
        microphone,
        reference,
        has_reference,
        aec_output
    );
    audio_input_ns_process(dsp, aec_output, output);
    *is_speech = audio_input_vad_process(dsp, output);
    // Gain runs after detection so calibration never changes what counts as
    // speech, and it only adapts on speech so silence cannot pump the gain.
    audio_input_agc_process(dsp, output, *is_speech);
}

uint32_t audio_input_dsp_aec_convergence_q10(const audio_input_dsp_t *dsp) {
    if (dsp == NULL) {
        return 0;
    }
    float energy = 0.0f;
    for (size_t coefficient = 0; coefficient < dsp->aec_filter_length; ++coefficient) {
        energy += dsp->aec_weights[coefficient] * dsp->aec_weights[coefficient];
    }
    const uint32_t index = (uint32_t)(sqrtf(energy) * 1024.0f);
    return index > 1024u ? 1024u : index;
}

uint32_t audio_input_dsp_agc_gain_q8(const audio_input_dsp_t *dsp) {
    if (dsp == NULL) {
        return 256u;
    }
    return (uint32_t)(dsp->agc_gain * 256.0f + 0.5f);
}
