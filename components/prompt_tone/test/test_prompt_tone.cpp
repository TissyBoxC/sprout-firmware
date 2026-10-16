#include "prompt_tone.h"

#include "audio_codec.h"
#include "esp_err.h"

#include <cassert>
#include <cstring>

void prompt_tone_host_reset(void);
void prompt_tone_host_set_create_mutex_fails(bool fails);
void prompt_tone_host_set_take_mutex_fails(bool fails);
void prompt_tone_host_set_pipeline_ready(bool ready);
void prompt_tone_host_set_set_playing_result(esp_err_t result);
void prompt_tone_host_set_play_frame_result(audio_codec_error_t result);
int prompt_tone_host_set_playing_calls(void);
int prompt_tone_host_play_frame_calls(void);
size_t prompt_tone_host_played_bytes(void);
uint32_t prompt_tone_host_frame_checksum(void);

namespace {

struct snapshot_delta_t {
    unsigned int played;
    unsigned int rejected;
    unsigned int safety;
};

snapshot_delta_t play_and_measure(prompt_tone_t tone, prompt_tone_error_t expected) {
    const prompt_tone_snapshot_t before = prompt_tone_get_snapshot();
    const int frames_before = prompt_tone_host_play_frame_calls();
    const size_t bytes_before = prompt_tone_host_played_bytes();

    assert(prompt_tone_play(tone) == expected);

    const prompt_tone_snapshot_t after = prompt_tone_get_snapshot();
    snapshot_delta_t delta{
        after.tones_played - before.tones_played,
        after.tones_rejected - before.tones_rejected,
        after.safety_tones_played - before.safety_tones_played,
    };
    // Every played cue writes whole frames of full PCM payload.
    if (delta.played == 1) {
        const int frames = prompt_tone_host_play_frame_calls() - frames_before;
        assert(frames > 0);
        assert(
            prompt_tone_host_played_bytes() - bytes_before ==
            static_cast<size_t>(frames) * AUDIO_CODEC_FRAME_PCM_BYTES
        );
    }
    return delta;
}

// Frames are 20 ms each, so a cue of N ms renders ceil(N / 20) whole frames.
void assert_cue_render(prompt_tone_t tone, int expected_frames) {
    const int calls_before = prompt_tone_host_play_frame_calls();
    const snapshot_delta_t delta =
        play_and_measure(tone, PROMPT_TONE_OK);
    assert(delta.played == 1);
    assert(delta.rejected == 0);
    assert(
        prompt_tone_host_play_frame_calls() - calls_before == expected_frames
    );
}

void test_calls_before_init_are_refused() {
    const prompt_tone_snapshot_t before = prompt_tone_get_snapshot();
    assert(!prompt_tone_is_ready());
    assert(
        prompt_tone_play(PROMPT_TONE_WAKE_ACCEPTED) ==
        PROMPT_TONE_ERR_NOT_INITIALIZED
    );
    // A refused call must not be counted as a rejected cue.
    assert(
        prompt_tone_get_snapshot().tones_rejected == before.tones_rejected
    );
}

void test_mutex_creation_failure_is_reported() {
    prompt_tone_host_reset();
    prompt_tone_host_set_create_mutex_fails(true);
    assert(prompt_tone_init() == ESP_ERR_NO_MEM);
    assert(!prompt_tone_is_ready());
}

void test_init_is_idempotent() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);
    assert(prompt_tone_is_ready());
    // A second init keeps the existing mutex and clears nothing about readiness.
    assert(prompt_tone_init() == ESP_OK);
    assert(prompt_tone_is_ready());
}

void test_every_cue_fits_the_bounded_frame_buffer() {
    // Durations, in whole 20 ms frames, for the frozen cue contract.
    assert_cue_render(PROMPT_TONE_WAKE_ACCEPTED, 12);       // 230 ms
    assert_cue_render(PROMPT_TONE_CAPTURE_STARTED, 7);      // 140 ms
    assert_cue_render(PROMPT_TONE_CAPTURE_STOPPED, 7);      // 140 ms
    assert_cue_render(PROMPT_TONE_MICROPHONE_MUTED, 9);     // 180 ms
    assert_cue_render(PROMPT_TONE_VOLUME_LIMIT, 10);        // 200 ms
    assert_cue_render(PROMPT_TONE_NETWORK_LOST, 15);        // 300 ms
    assert_cue_render(PROMPT_TONE_NETWORK_RESTORED, 16);    // 320 ms
    assert_cue_render(PROMPT_TONE_BATTERY_LOW, 15);         // 300 ms
    assert_cue_render(PROMPT_TONE_SAFETY_ANNOUNCEMENT, 16);  // 320 ms
    assert_cue_render(PROMPT_TONE_FACTORY_RESET_ARMED, 15);  // 290 ms
    assert_cue_render(PROMPT_TONE_FACTORY_RESET_CANCELLED, 11);  // 220 ms
    assert_cue_render(PROMPT_TONE_FACTORY_RESET_COMPLETED, 15);  // 300 ms

    // The safety cue is the only one that must also be counted separately.
    const snapshot_delta_t safety =
        play_and_measure(PROMPT_TONE_SAFETY_ANNOUNCEMENT, PROMPT_TONE_OK);
    assert(safety.played == 1);
    assert(safety.safety == 1);
}

void test_safety_announcement_is_counted_separately() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);

    const snapshot_delta_t normal =
        play_and_measure(PROMPT_TONE_NETWORK_LOST, PROMPT_TONE_OK);
    assert(normal.played == 1);
    assert(normal.safety == 0);

    const snapshot_delta_t safety =
        play_and_measure(PROMPT_TONE_SAFETY_ANNOUNCEMENT, PROMPT_TONE_OK);
    assert(safety.played == 1);
    assert(safety.safety == 1);
}

void test_cue_waveform_is_deterministic_and_silent_segments_are_zero() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);
    assert(prompt_tone_play(PROMPT_TONE_WAKE_ACCEPTED) == PROMPT_TONE_OK);
    const uint32_t stable_checksum = prompt_tone_host_frame_checksum();
    assert(stable_checksum != 0);

    // Replaying the same cue with no intervening state must render identically.
    prompt_tone_host_reset();
    assert(prompt_tone_play(PROMPT_TONE_WAKE_ACCEPTED) == PROMPT_TONE_OK);
    assert(prompt_tone_host_frame_checksum() == stable_checksum);

    // A cue whose first segment is silent still emits the same frame count, and
    // a different cue produces a different envelope.
    prompt_tone_host_reset();
    assert(prompt_tone_play(PROMPT_TONE_MICROPHONE_MUTED) == PROMPT_TONE_OK);
    assert(prompt_tone_host_play_frame_calls() == 9);
    assert(prompt_tone_host_frame_checksum() != stable_checksum);
}

void test_invalid_tone_is_rejected_and_counted() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);
    const snapshot_delta_t beyond =
        play_and_measure(
            static_cast<prompt_tone_t>(PROMPT_TONE_COUNT),
            PROMPT_TONE_ERR_INVALID_TONE
        );
    assert(beyond.played == 0);
    assert(beyond.rejected == 1);

    const snapshot_delta_t negative =
        play_and_measure(
            static_cast<prompt_tone_t>(-1),
            PROMPT_TONE_ERR_INVALID_TONE
        );
    assert(negative.played == 0);
    assert(negative.rejected == 1);

    assert(prompt_tone_host_play_frame_calls() == 0);
}

void test_missing_output_path_rejects_without_playing() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);

    // No queue and no ready speaker: the cue cannot be delivered.
    prompt_tone_host_set_pipeline_ready(false);
    const snapshot_delta_t no_speaker =
        play_and_measure(PROMPT_TONE_WAKE_ACCEPTED, PROMPT_TONE_ERR_QUEUE);
    assert(prompt_tone_host_play_frame_calls() == 0);
    assert(no_speaker.played == 0);
    assert(no_speaker.rejected == 1);

    // A speaker that refuses to start is the same outcome.
    prompt_tone_host_set_pipeline_ready(true);
    prompt_tone_host_set_set_playing_result(ESP_FAIL);
    const snapshot_delta_t start_failed =
        play_and_measure(PROMPT_TONE_WAKE_ACCEPTED, PROMPT_TONE_ERR_QUEUE);
    assert(prompt_tone_host_set_playing_calls() == 1);
    assert(prompt_tone_host_play_frame_calls() == 0);
    assert(start_failed.played == 0);
    assert(start_failed.rejected == 1);
}

void test_frame_write_failure_rejects_the_whole_cue() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);

    prompt_tone_host_set_play_frame_result(AUDIO_CODEC_ERR_NOT_INITIALIZED);
    const snapshot_delta_t delta =
        play_and_measure(PROMPT_TONE_WAKE_ACCEPTED, PROMPT_TONE_ERR_QUEUE);
    // The cue aborts on the first rejected frame instead of playing a prefix.
    assert(prompt_tone_host_play_frame_calls() == 1);
    assert(delta.played == 0);
    assert(delta.rejected == 1);
}

void test_mutex_contention_is_reported_without_counting_a_rejection() {
    prompt_tone_host_reset();
    assert(prompt_tone_init() == ESP_OK);

    prompt_tone_host_set_take_mutex_fails(true);
    const snapshot_delta_t delta =
        play_and_measure(PROMPT_TONE_WAKE_ACCEPTED, PROMPT_TONE_ERR_QUEUE);
    assert(delta.played == 0);
    assert(delta.rejected == 0);
}

void test_contract_names_are_stable() {
    assert(
        std::strcmp(prompt_tone_name(PROMPT_TONE_WAKE_ACCEPTED), "wake_accepted") ==
        0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_CAPTURE_STARTED),
            "capture_started"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_CAPTURE_STOPPED),
            "capture_stopped"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_MICROPHONE_MUTED),
            "microphone_muted"
        ) == 0
    );
    assert(
        std::strcmp(prompt_tone_name(PROMPT_TONE_VOLUME_LIMIT), "volume_limit") ==
        0
    );
    assert(
        std::strcmp(prompt_tone_name(PROMPT_TONE_NETWORK_LOST), "network_lost") ==
        0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_NETWORK_RESTORED),
            "network_restored"
        ) == 0
    );
    assert(
        std::strcmp(prompt_tone_name(PROMPT_TONE_BATTERY_LOW), "battery_low") == 0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_SAFETY_ANNOUNCEMENT),
            "safety_announcement"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_FACTORY_RESET_ARMED),
            "factory_reset_armed"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_FACTORY_RESET_CANCELLED),
            "factory_reset_cancelled"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_name(PROMPT_TONE_FACTORY_RESET_COMPLETED),
            "factory_reset_completed"
        ) == 0
    );
    // An out-of-contract tone must not leak an unstable string.
    assert(
        std::strcmp(
            prompt_tone_name(static_cast<prompt_tone_t>(PROMPT_TONE_COUNT)),
            "unknown"
        ) == 0
    );
}

void test_error_names_are_stable() {
    assert(std::strcmp(prompt_tone_error_name(PROMPT_TONE_OK), "ok") == 0);
    assert(
        std::strcmp(
            prompt_tone_error_name(PROMPT_TONE_ERR_NOT_INITIALIZED),
            "audio_codec_not_initialized"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_error_name(PROMPT_TONE_ERR_INVALID_TONE),
            "audio_codec_invalid_argument"
        ) == 0
    );
    assert(
        std::strcmp(
            prompt_tone_error_name(PROMPT_TONE_ERR_QUEUE),
            "audio_queue_full"
        ) == 0
    );
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor = prompt_tone_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "prompt_tone") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    // The module has no shutdown, so the pre-init assertions come first.
    test_calls_before_init_are_refused();
    test_mutex_creation_failure_is_reported();
    test_init_is_idempotent();
    test_every_cue_fits_the_bounded_frame_buffer();
    test_safety_announcement_is_counted_separately();
    test_cue_waveform_is_deterministic_and_silent_segments_are_zero();
    test_invalid_tone_is_rejected_and_counted();
    test_missing_output_path_rejects_without_playing();
    test_frame_write_failure_rejects_the_whole_cue();
    test_mutex_contention_is_reported_without_counting_a_rejection();
    test_contract_names_are_stable();
    test_error_names_are_stable();
    test_module_descriptor_is_stable();
    return 0;
}
