#include "voice_wake.h"

#include "audio_pipeline.h"
#include "esp_err.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

void voice_wake_host_reset(void);
void voice_wake_host_reset_state(void);
void voice_wake_host_process_one_frame(void);
void voice_wake_host_set_pipeline_ready(bool ready);
void voice_wake_host_set_pipeline_playing(bool playing);
void voice_wake_host_set_pipeline_capturing(bool capturing);
void voice_wake_host_set_capture_result(audio_codec_error_t result);
void voice_wake_host_set_create_mutex_result(int ok);
void voice_wake_host_set_create_binary_result(int ok);
void voice_wake_host_set_task_create_result(int ok);
void voice_wake_host_set_now_ms(uint64_t now_ms);
void voice_wake_host_advance_ms(uint64_t delta_ms);
void voice_wake_host_set_signal_tokens(int tokens);
int voice_wake_host_signal_tokens(void);
int voice_wake_host_task_create_calls(void);
int voice_wake_host_capture_calls(void);
bool voice_wake_host_task_registered(void);
audio_pipeline_capture_owner_t voice_wake_host_capture_owner(void);
void voice_wake_host_run_task_once(void);
void voice_wake_host_complete_task(void);
void voice_wake_host_set_frame_value(int16_t value, uint32_t sequence);

static int wake_calls;
static voice_wake_event_t last_event;
static void *last_context;

static void wake_handler(const voice_wake_event_t *event, void *context) {
    wake_calls++;
    last_event = *event;
    last_context = context;
}

static const module_descriptor_t *descriptor(void) {
    const module_descriptor_t *value = voice_wake_module_descriptor();
    assert(value != NULL);
    assert(value->initialize != NULL);
    assert(value->shutdown != NULL);
    return value;
}

static void prepare(void) {
    voice_wake_host_reset();
    voice_wake_host_reset_state();
    voice_wake_host_set_now_ms(1000);
    assert(descriptor()->initialize() == ESP_OK);
    assert(voice_wake_is_ready());
    wake_calls = 0;
    memset(&last_event, 0, sizeof(last_event));
    last_context = NULL;
}

static void test_init_requires_pipeline_and_allocations(void) {
    voice_wake_host_reset();
    voice_wake_host_reset_state();
    voice_wake_host_set_pipeline_ready(false);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);
    assert(!voice_wake_is_ready());

    voice_wake_host_reset();
    voice_wake_host_reset_state();
    voice_wake_host_set_create_mutex_result(0);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);

    voice_wake_host_reset();
    voice_wake_host_reset_state();
    voice_wake_host_set_create_binary_result(0);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);
    assert(!voice_wake_is_ready());
}

static void test_start_acquires_the_capture_lease_and_rolls_back(void) {
    prepare();
    assert(voice_wake_start() == ESP_OK);
    assert(voice_wake_host_capture_owner() ==
           AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE);
    assert(voice_wake_host_task_registered());
    assert(voice_wake_host_task_create_calls() == 1);

    voice_wake_host_reset();
    voice_wake_host_reset_state();
    voice_wake_host_set_task_create_result(0);
    voice_wake_host_set_now_ms(1000);
    assert(descriptor()->initialize() == ESP_OK);
    assert(voice_wake_start() == ESP_ERR_NO_MEM);
    assert(voice_wake_host_capture_owner() ==
           AUDIO_PIPELINE_CAPTURE_OWNER_NONE);
}

static void test_stop_releases_capture_and_disarms(void) {
    prepare();
    assert(voice_wake_start() == ESP_OK);
    voice_wake_host_complete_task();
    assert(voice_wake_stop() == ESP_OK);
    assert(voice_wake_host_capture_owner() ==
           AUDIO_PIPELINE_CAPTURE_OWNER_NONE);

    voice_wake_snapshot_t snapshot;
    assert(voice_wake_get_snapshot(&snapshot) == ESP_OK);
    assert(!snapshot.is_armed);
    assert(!snapshot.is_suspended);
}

static void test_suspend_and_resume_preserve_the_arm_state(void) {
    prepare();
    assert(voice_wake_start() == ESP_OK);
    voice_wake_host_complete_task();
    assert(voice_wake_suspend() == ESP_OK);
    assert(voice_wake_host_capture_owner() ==
           AUDIO_PIPELINE_CAPTURE_OWNER_NONE);

    voice_wake_snapshot_t snapshot;
    assert(voice_wake_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.is_armed);
    assert(snapshot.is_suspended);

    assert(voice_wake_resume() == ESP_OK);
    assert(voice_wake_host_capture_owner() ==
           AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE);
    assert(voice_wake_get_snapshot(&snapshot) == ESP_OK);
    assert(!snapshot.is_suspended);
}

static void test_suspend_rejects_when_not_armed(void) {
    prepare();
    assert(voice_wake_suspend() == ESP_ERR_INVALID_STATE);
    assert(voice_wake_resume() == ESP_ERR_INVALID_STATE);
}

static void test_deterministic_detection_publishes_a_bounded_event(void) {
    prepare();
    assert(voice_wake_set_wake_handler(wake_handler, (void *)0x1234) == ESP_OK);
    assert(voice_wake_start() == ESP_OK);
    voice_wake_host_set_frame_value(2000, 7);

    for (int index = 0;
         index < CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES;
         ++index) {
        voice_wake_host_run_task_once();
    }

    assert(voice_wake_host_capture_calls() ==
           CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES);
    voice_wake_snapshot_t snapshot;
    assert(voice_wake_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.frames_processed ==
           CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES);
    assert(snapshot.rejected_frames == 0);
    assert(snapshot.false_wake_rejections == 0);
    assert(snapshot.detections == 1);
    assert(wake_calls == 1);
    assert(last_context == (void *)0x1234);
    assert(last_event.wake_word_id == 1);
    assert(strcmp(last_event.wake_word_name, "deterministic_energy") == 0);
    assert(strcmp(last_event.detail_code, "wake_1_confidence_1000") == 0);
    assert(last_event.confidence_milli == 1000);
    assert(last_event.session_nonce == 1);
}

static void test_playback_and_cooldown_suppress_detection(void) {
    prepare();
    assert(voice_wake_set_wake_handler(wake_handler, NULL) == ESP_OK);
    assert(voice_wake_start() == ESP_OK);

    voice_wake_host_set_pipeline_playing(true);
    for (int index = 0; index < 8; ++index) {
        voice_wake_host_run_task_once();
    }
    assert(wake_calls == 0);

    voice_wake_host_set_pipeline_playing(false);
    voice_wake_host_advance_ms(1);
    for (int index = 0; index < 8; ++index) {
        voice_wake_host_run_task_once();
    }
    assert(wake_calls == 0);

    voice_wake_host_advance_ms(CONFIG_VOICE_WAKE_PLAYBACK_IGNORE_MS + 1);
    for (int index = 0;
         index < CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES;
         ++index) {
        voice_wake_host_run_task_once();
    }
    assert(wake_calls == 1);

    voice_wake_host_advance_ms(1);
    for (int index = 0;
         index < CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES;
         ++index) {
        voice_wake_host_run_task_once();
    }
    assert(wake_calls == 1);

    voice_wake_host_advance_ms(CONFIG_VOICE_WAKE_COOLDOWN_MS + 1);
    for (int index = 0;
         index < CONFIG_VOICE_WAKE_DETERMINISTIC_MIN_RUN_FRAMES;
         ++index) {
        voice_wake_host_run_task_once();
    }
    assert(wake_calls == 2);
}

static void test_capture_failure_is_counted_and_snapshot_is_bounded(void) {
    prepare();
    assert(voice_wake_start() == ESP_OK);
    voice_wake_host_set_capture_result(AUDIO_CODEC_ERR_DECODE_FAILED);
    voice_wake_host_run_task_once();

    voice_wake_snapshot_t snapshot;
    assert(voice_wake_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.frames_processed == 0);
    assert(snapshot.rejected_frames == 1);
    assert(snapshot.detections == 0);
    assert(voice_wake_get_snapshot(NULL) == ESP_ERR_INVALID_ARG);
}

static void test_error_names_are_stable(void) {
    assert(strcmp(voice_wake_error_name(VOICE_WAKE_ERROR_NONE), "none") == 0);
    assert(strcmp(
        voice_wake_error_name(VOICE_WAKE_ERROR_NOT_INITIALIZED),
        "voice_wake_not_initialized"
    ) == 0);
    assert(strcmp(
        voice_wake_error_name(VOICE_WAKE_ERROR_INVALID_STATE),
        "voice_wake_invalid_state"
    ) == 0);
}

int main(void) {
    test_init_requires_pipeline_and_allocations();
    test_start_acquires_the_capture_lease_and_rolls_back();
    test_stop_releases_capture_and_disarms();
    test_suspend_and_resume_preserve_the_arm_state();
    test_suspend_rejects_when_not_armed();
    test_deterministic_detection_publishes_a_bounded_event();
    test_playback_and_cooldown_suppress_detection();
    test_capture_failure_is_counted_and_snapshot_is_bounded();
    test_error_names_are_stable();
    return 0;
}
