#include "audio_output.h"

#include "audio_codec.h"
#include "esp_err.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void audio_output_host_reset(void);
void audio_output_host_set_pipeline_ready(bool ready);
void audio_output_host_set_set_playing_result(esp_err_t result);
void audio_output_host_set_play_result(audio_codec_error_t result);
void audio_output_host_set_task_create_result(int ok);
void audio_output_host_fail_alloc_after(int successful_allocations);
void audio_output_host_set_volume(
    bool ready,
    uint8_t percent,
    uint8_t maximum_percent,
    bool muted
);
int audio_output_host_play_calls(void);
int audio_output_host_task_create_calls(void);
int audio_output_host_alloc_calls(void);
int audio_output_host_signal_tokens(void);
bool audio_output_host_last_frame(audio_codec_pcm_frame_t *frame_out);
bool audio_output_host_mix_one_frame(void);

static audio_codec_pcm_frame_t make_frame(int16_t value, uint32_t sequence) {
    audio_codec_pcm_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.sequence = sequence;
    frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    for (size_t index = 0; index < AUDIO_CODEC_SAMPLES_PER_FRAME; ++index) {
        frame.pcm[index] = value;
    }
    return frame;
}

static const module_descriptor_t *descriptor(void) {
    const module_descriptor_t *value = audio_output_module_descriptor();
    assert(value != NULL);
    assert(value->initialize != NULL);
    assert(value->shutdown != NULL);
    return value;
}

static void prepare(void) {
    descriptor()->shutdown();
    audio_output_host_reset();
    /*
     * The host reset clears the stub semaphore table, so the module must
     * perform a fresh initialize after that reset. shutdown() above clears
     * audio_output_ready, which makes the following call rebuild resources.
     */
    assert(descriptor()->initialize() == ESP_OK);
    assert(audio_output_is_ready());
}

static void mix_all(void) {
    while (audio_output_host_mix_one_frame()) {
    }
}

static void test_calls_before_init_are_refused(void) {
    descriptor()->shutdown();
    audio_output_host_reset();

    assert(!audio_output_is_ready());
    const audio_codec_pcm_frame_t frame = make_frame(100, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_ERR_NOT_INITIALIZED
    );
    assert(
        audio_output_submit_blocking(
            &frame,
            AUDIO_OUTPUT_PRIORITY_CONVERSATION
        ) == AUDIO_OUTPUT_ERR_NOT_INITIALIZED
    );
    assert(
        audio_output_discard_priority(
            AUDIO_OUTPUT_PRIORITY_CONVERSATION,
            NULL
        ) == AUDIO_OUTPUT_ERR_NOT_INITIALIZED
    );
    assert(
        audio_output_flush(AUDIO_OUTPUT_PRIORITY_SAFETY, true, NULL) ==
        AUDIO_OUTPUT_ERR_NOT_INITIALIZED
    );
    assert(!audio_output_get_snapshot().is_ready);
}

static void test_init_requires_pipeline_and_reports_allocation_failure(void) {
    descriptor()->shutdown();
    audio_output_host_reset();
    audio_output_host_set_pipeline_ready(false);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);
    assert(!audio_output_is_ready());

    audio_output_host_reset();
    audio_output_host_fail_alloc_after(0);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);
    assert(!audio_output_is_ready());
    assert(audio_output_host_alloc_calls() == 1);

    audio_output_host_reset();
    audio_output_host_set_task_create_result(0);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);
    assert(!audio_output_is_ready());
}

static void test_init_is_idempotent(void) {
    prepare();
    assert(descriptor()->initialize() == ESP_OK);
    assert(audio_output_host_task_create_calls() == 1);
}

static void test_submit_validates_arguments_and_pcm_size(void) {
    prepare();

    const audio_codec_pcm_frame_t frame = make_frame(100, 1);
    assert(
        audio_output_submit(NULL, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_ERR_INVALID_ARGUMENT
    );
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_COUNT) ==
        AUDIO_OUTPUT_ERR_INVALID_ARGUMENT
    );
    assert(
        audio_output_submit(&frame, (audio_output_priority_t)-1) ==
        AUDIO_OUTPUT_ERR_INVALID_ARGUMENT
    );

    audio_codec_pcm_frame_t short_frame = frame;
    short_frame.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES - 2;
    assert(
        audio_output_submit(&short_frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE
    );
}

static void test_submit_tracks_pending_frames_per_lane(void) {
    prepare();
    const audio_codec_pcm_frame_t frame = make_frame(100, 1);

    for (int priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        assert(audio_output_submit(
            &frame,
            (audio_output_priority_t)priority
        ) == AUDIO_OUTPUT_OK);
    }

    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.is_ready);
    assert(snapshot.submitted_frames == 4);
    for (int priority = AUDIO_OUTPUT_PRIORITY_AMBIENT;
         priority < AUDIO_OUTPUT_PRIORITY_COUNT;
         ++priority) {
        assert(snapshot.pending_frames[priority] == 1);
    }
    assert(snapshot.has_primary_source);
}

static void test_queue_full_is_reported_without_consuming_the_frame(void) {
    prepare();
    const audio_codec_pcm_frame_t frame = make_frame(100, 1);

    for (int index = 0; index < CONFIG_AUDIO_OUTPUT_QUEUE_FRAMES; ++index) {
        assert(
            audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
            AUDIO_OUTPUT_OK
        );
    }
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_ERR_QUEUE_FULL
    );

    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(
        snapshot.pending_frames[AUDIO_OUTPUT_PRIORITY_CONVERSATION] ==
        (uint32_t)CONFIG_AUDIO_OUTPUT_QUEUE_FRAMES
    );
    assert(snapshot.queue_full_frames >= 1u);
}

static void test_discard_priority_only_clears_the_requested_lane(void) {
    prepare();
    const audio_codec_pcm_frame_t frame = make_frame(100, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_PROMPT) ==
        AUDIO_OUTPUT_OK
    );
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_SAFETY) ==
        AUDIO_OUTPUT_OK
    );

    size_t discarded = 0;
    assert(
        audio_output_discard_priority(
            AUDIO_OUTPUT_PRIORITY_PROMPT,
            &discarded
        ) == AUDIO_OUTPUT_OK
    );
    assert(discarded == 1);

    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.pending_frames[AUDIO_OUTPUT_PRIORITY_PROMPT] == 0);
    assert(snapshot.pending_frames[AUDIO_OUTPUT_PRIORITY_SAFETY] == 1);
    assert(snapshot.discarded_frames == 1);
}

static void test_flush_can_protect_or_remove_safety_frames(void) {
    prepare();
    const audio_codec_pcm_frame_t frame = make_frame(100, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_OK
    );
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_SAFETY) ==
        AUDIO_OUTPUT_OK
    );

    size_t discarded = 0;
    assert(
        audio_output_flush(
            AUDIO_OUTPUT_PRIORITY_SAFETY,
            false,
            &discarded
        ) == AUDIO_OUTPUT_OK
    );
    assert(discarded == 1);
    audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.pending_frames[AUDIO_OUTPUT_PRIORITY_CONVERSATION] == 0);
    assert(snapshot.pending_frames[AUDIO_OUTPUT_PRIORITY_SAFETY] == 1);

    assert(
        audio_output_flush(
            AUDIO_OUTPUT_PRIORITY_SAFETY,
            true,
            &discarded
        ) == AUDIO_OUTPUT_OK
    );
    assert(discarded == 1);
    snapshot = audio_output_get_snapshot();
    assert(snapshot.pending_frames[AUDIO_OUTPUT_PRIORITY_SAFETY] == 0);
}

static void test_mix_renders_and_applies_volume(void) {
    prepare();
    audio_output_host_set_volume(true, 50, 100, false);
    const audio_codec_pcm_frame_t frame = make_frame(1000, 7);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_OK
    );
    assert(audio_output_host_signal_tokens() > 0);

    mix_all();

    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.output_frames == 1);
    assert(snapshot.mixed_frames == 1);
    assert(snapshot.applied_volume_percent == 50);
    assert(audio_output_host_play_calls() == 1);

    audio_codec_pcm_frame_t rendered;
    memset(&rendered, 0, sizeof(rendered));
    assert(audio_output_host_last_frame(&rendered));
    assert(rendered.pcm[0] == 500);
}

static void test_normal_mute_is_silent_but_still_counted(void) {
    prepare();
    audio_output_host_set_volume(true, 0, 80, true);
    const audio_codec_pcm_frame_t frame = make_frame(1000, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_OK
    );
    mix_all();

    assert(audio_output_host_play_calls() == 1);
    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.applied_volume_percent == 0);
    audio_codec_pcm_frame_t rendered;
    memset(&rendered, 0, sizeof(rendered));
    assert(audio_output_host_last_frame(&rendered));
    assert(rendered.pcm[0] == 0);
}

static void test_safety_gain_uses_maximum_or_minimum(void) {
    prepare();
    audio_output_host_set_volume(true, 0, 80, true);
    const audio_codec_pcm_frame_t frame = make_frame(1000, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_SAFETY) ==
        AUDIO_OUTPUT_OK
    );
    mix_all();
    assert(audio_output_get_snapshot().applied_volume_percent == 80);

    audio_output_host_set_volume(true, 0, 0, true);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_SAFETY) ==
        AUDIO_OUTPUT_OK
    );
    mix_all();
    assert(
        audio_output_get_snapshot().applied_volume_percent ==
        (uint8_t)CONFIG_AUDIO_OUTPUT_SAFETY_MIN_PERCENT
    );
}

static void test_play_frame_error_is_counted(void) {
    prepare();
    audio_output_host_set_play_result(AUDIO_CODEC_ERR_DECODE_FAILED);
    const audio_codec_pcm_frame_t frame = make_frame(1000, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_OK
    );
    mix_all();

    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.output_errors == 1);
    assert(snapshot.output_frames == 0);
}

static void test_enable_playing_failure_is_counted(void) {
    prepare();
    audio_output_host_set_set_playing_result(ESP_ERR_INVALID_STATE);
    const audio_codec_pcm_frame_t frame = make_frame(1000, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_OK
    );
    mix_all();

    const audio_output_snapshot_t snapshot = audio_output_get_snapshot();
    assert(snapshot.output_errors == 1);
    assert(audio_output_host_play_calls() == 0);
}

static void test_priority_and_error_names_are_stable(void) {
    assert(strcmp(
        audio_output_priority_name(AUDIO_OUTPUT_PRIORITY_AMBIENT),
        "ambient"
    ) == 0);
    assert(strcmp(
        audio_output_priority_name(AUDIO_OUTPUT_PRIORITY_CONVERSATION),
        "conversation"
    ) == 0);
    assert(strcmp(
        audio_output_priority_name(AUDIO_OUTPUT_PRIORITY_PROMPT),
        "prompt"
    ) == 0);
    assert(strcmp(
        audio_output_priority_name(AUDIO_OUTPUT_PRIORITY_SAFETY),
        "safety"
    ) == 0);
    assert(strcmp(
        audio_output_priority_name((audio_output_priority_t)99),
        "ambient"
    ) == 0);

    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_NOT_INITIALIZED),
        "audio_codec_not_initialized"
    ) == 0);
    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_INVALID_ARGUMENT),
        "audio_codec_invalid_argument"
    ) == 0);
    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE),
        "audio_codec_invalid_pcm_size"
    ) == 0);
    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_QUEUE_FULL),
        "audio_queue_full"
    ) == 0);
    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_DISCARDED),
        "audio_output_discarded"
    ) == 0);
    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_TIMEOUT),
        "audio_output_timeout"
    ) == 0);
    assert(strcmp(
        audio_output_error_name(AUDIO_OUTPUT_ERR_INTERNAL),
        "audio_codec_internal"
    ) == 0);
}

static void test_shutdown_clears_snapshot_and_allows_reinit(void) {
    prepare();
    const audio_codec_pcm_frame_t frame = make_frame(100, 1);
    assert(
        audio_output_submit(&frame, AUDIO_OUTPUT_PRIORITY_CONVERSATION) ==
        AUDIO_OUTPUT_OK
    );
    descriptor()->shutdown();
    assert(!audio_output_is_ready());
    assert(!audio_output_get_snapshot().is_ready);

    audio_output_host_reset();
    assert(descriptor()->initialize() == ESP_OK);
    assert(audio_output_is_ready());
    descriptor()->shutdown();
}

static void test_module_descriptor_is_stable(void) {
    const module_descriptor_t *value = audio_output_module_descriptor();
    assert(value != NULL);
    assert(strcmp(value->module_name, "audio_output") == 0);
    assert(strcmp(value->version, "1.0.0") == 0);
    assert(value->initialize != NULL);
    assert(value->shutdown != NULL);
}

int main(void) {
    test_calls_before_init_are_refused();
    test_init_requires_pipeline_and_reports_allocation_failure();
    test_init_is_idempotent();
    test_submit_validates_arguments_and_pcm_size();
    test_submit_tracks_pending_frames_per_lane();
    test_queue_full_is_reported_without_consuming_the_frame();
    test_discard_priority_only_clears_the_requested_lane();
    test_flush_can_protect_or_remove_safety_frames();
    test_mix_renders_and_applies_volume();
    test_normal_mute_is_silent_but_still_counted();
    test_safety_gain_uses_maximum_or_minimum();
    test_play_frame_error_is_counted();
    test_enable_playing_failure_is_counted();
    test_priority_and_error_names_are_stable();
    test_shutdown_clears_snapshot_and_allows_reinit();
    test_module_descriptor_is_stable();
    return 0;
}
