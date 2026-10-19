#include "playback_queue.h"

#include "audio_codec.h"
#include "esp_err.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void playback_queue_host_reset(void);
void playback_queue_host_set_create_mutex_fails(bool fails);
void playback_queue_host_set_create_binary_fails(bool fails);
void playback_queue_host_set_create_task_fails(bool fails);
void playback_queue_host_set_pipeline_ready(bool ready);
void playback_queue_host_set_playing_result(esp_err_t result);
void playback_queue_host_set_play_frame_result(audio_codec_error_t result);
void playback_queue_host_fail_alloc_after(int successful_allocations);
int playback_queue_host_task_create_calls(void);
int playback_queue_host_play_frame_calls(void);

static const module_descriptor_t *descriptor(void) {
    const module_descriptor_t *value = playback_queue_module_descriptor();
    assert(value != NULL);
    assert(value->initialize != NULL);
    assert(value->shutdown != NULL);
    return value;
}

/* The queue has a shutdown, so each case can start from a clean module. */
static void prepare(void) {
    descriptor()->shutdown();
    playback_queue_host_reset();
    assert(descriptor()->initialize() == ESP_OK);
    assert(playback_queue_is_ready());
}

static playback_queue_item_t make_item(
    const char *id,
    playback_priority_t priority,
    size_t frame_count,
    bool interruptible
) {
    playback_queue_item_t item;
    memset(&item, 0, sizeof(item));
    strncpy(item.item_id, id, sizeof(item.item_id) - 1);
    item.priority = priority;
    item.is_interruptible = interruptible;
    const size_t initialized_frames =
        frame_count < PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM
            ? frame_count
            : PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM;
    item.frame_count = initialized_frames;
    for (size_t index = 0; index < initialized_frames; ++index) {
        item.frames[index].pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    }
    return item;
}

static playback_queue_item_t make_interruptible(
    const char *id,
    playback_priority_t priority,
    size_t frame_count
) {
    return make_item(id, priority, frame_count, true);
}

static void test_calls_before_init_are_refused(void) {
    descriptor()->shutdown();
    playback_queue_host_reset();
    playback_queue_item_t item =
        make_interruptible("before", PLAYBACK_PRIORITY_PROMPT, 1);

    assert(!playback_queue_is_ready());
    assert(
        playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_NOT_INITIALIZED
    );
    assert(playback_queue_pause() == ESP_ERR_INVALID_STATE);
    assert(playback_queue_resume() == ESP_ERR_INVALID_STATE);
    assert(playback_queue_clear(true) == ESP_ERR_INVALID_STATE);
}

static void test_init_failures_are_reported(void) {
    descriptor()->shutdown();
    playback_queue_host_reset();
    playback_queue_host_set_pipeline_ready(false);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);
    assert(!playback_queue_is_ready());

    playback_queue_host_reset();
    playback_queue_host_set_create_mutex_fails(true);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);
    assert(!playback_queue_is_ready());

    playback_queue_host_reset();
    playback_queue_host_set_create_binary_fails(true);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);

    playback_queue_host_reset();
    playback_queue_host_set_create_task_fails(true);
    assert(descriptor()->initialize() == ESP_ERR_NO_MEM);
    assert(!playback_queue_is_ready());
}

static void test_init_is_idempotent(void) {
    prepare();
    assert(descriptor()->initialize() == ESP_OK);
    assert(playback_queue_host_task_create_calls() == 1);
}

static void test_enqueue_validates_the_item(void) {
    prepare();

    assert(playback_queue_enqueue(NULL) == PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT);

    playback_queue_item_t item =
        make_interruptible("", PLAYBACK_PRIORITY_PROMPT, 1);
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT);

    item = make_interruptible("zero", PLAYBACK_PRIORITY_PROMPT, 0);
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT);

    item = make_interruptible(
        "too-many",
        PLAYBACK_PRIORITY_PROMPT,
        PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM
    );
    item.frame_count = PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM + 1;
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT);
}

static void test_enqueue_rejects_partial_frames(void) {
    prepare();
    playback_queue_item_t item =
        make_interruptible("partial", PLAYBACK_PRIORITY_CONVERSATION, 3);
    item.frames[2].pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES - 2;
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_EMPTY_PAYLOAD);
    assert(playback_queue_get_snapshot().pending_items == 0);
}

static void test_enqueue_accepts_a_full_item(void) {
    prepare();
    playback_queue_item_t item = make_interruptible(
        "full",
        PLAYBACK_PRIORITY_CONVERSATION,
        PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM
    );
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_OK);
    assert(playback_queue_get_snapshot().pending_items == 1);
}

static void test_duplicate_identifier_is_refused(void) {
    prepare();
    playback_queue_item_t item =
        make_interruptible("same-id", PLAYBACK_PRIORITY_CONVERSATION, 1);
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_OK);
    assert(
        playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_DUPLICATE_ITEM
    );
    assert(playback_queue_get_snapshot().pending_items == 1);
}

static void test_safety_priority_is_protected_from_clear(void) {
    prepare();
    playback_queue_item_t item =
        make_item("safety", PLAYBACK_PRIORITY_SAFETY, 2, true);
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_OK);

    /* A generic clear must keep the pending safety announcement. */
    assert(playback_queue_clear(false) == ESP_OK);
    assert(playback_queue_get_snapshot().pending_items == 1);

    /* A full clear during teardown does remove it. */
    assert(playback_queue_clear(true) == ESP_OK);
    assert(playback_queue_get_snapshot().pending_items == 0);
}

static void test_queue_capacity_is_bounded(void) {
    prepare();
    for (int index = 0; index < CONFIG_PLAYBACK_QUEUE_CAPACITY; ++index) {
        char id[16];
        snprintf(id, sizeof(id), "item-%d", index);
        playback_queue_item_t item =
            make_interruptible(id, PLAYBACK_PRIORITY_CONVERSATION, 1);
        assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_OK);
    }
    playback_queue_item_t overflow =
        make_interruptible("overflow", PLAYBACK_PRIORITY_CONVERSATION, 1);
    assert(playback_queue_enqueue(&overflow) == PLAYBACK_QUEUE_ERR_FULL);
    assert(
        playback_queue_get_snapshot().pending_items ==
        (size_t)CONFIG_PLAYBACK_QUEUE_CAPACITY
    );
}

static void test_allocation_failure_is_reported_as_full(void) {
    prepare();
    playback_queue_host_fail_alloc_after(0);
    playback_queue_item_t item =
        make_interruptible("alloc", PLAYBACK_PRIORITY_CONVERSATION, 1);
    assert(playback_queue_enqueue(&item) == PLAYBACK_QUEUE_ERR_FULL);
}

static void test_pause_and_resume_track_state(void) {
    prepare();
    assert(playback_queue_pause() == ESP_OK);
    assert(playback_queue_get_snapshot().is_paused);
    assert(playback_queue_resume() == ESP_OK);
    assert(!playback_queue_get_snapshot().is_paused);
}

static void test_priority_and_error_names_are_stable(void) {
    assert(
        strcmp(playback_priority_name(PLAYBACK_PRIORITY_AMBIENT), "ambient") == 0
    );
    assert(
        strcmp(
            playback_priority_name(PLAYBACK_PRIORITY_CONVERSATION),
            "conversation"
        ) == 0
    );
    assert(
        strcmp(playback_priority_name(PLAYBACK_PRIORITY_PROMPT), "prompt") == 0
    );
    assert(
        strcmp(playback_priority_name(PLAYBACK_PRIORITY_SAFETY), "safety") == 0
    );
    assert(
        strcmp(playback_priority_name((playback_priority_t)99), "ambient") == 0
    );

    assert(strcmp(playback_queue_error_name(PLAYBACK_QUEUE_OK), "ok") == 0);
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_NOT_INITIALIZED),
            "audio_codec_not_initialized"
        ) == 0
    );
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT),
            "audio_codec_invalid_argument"
        ) == 0
    );
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_FULL),
            "audio_queue_full"
        ) == 0
    );
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_DUPLICATE_ITEM),
            "audio_queue_duplicate_item"
        ) == 0
    );
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_EMPTY_PAYLOAD),
            "audio_queue_payload_empty"
        ) == 0
    );
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_INTERRUPT_DENIED),
            "audio_queue_interrupt_denied"
        ) == 0
    );
    assert(
        strcmp(
            playback_queue_error_name(PLAYBACK_QUEUE_ERR_NOT_FOUND),
            "audio_codec_internal"
        ) == 0
    );
}

static void test_shutdown_clears_pending_items_and_allows_reinit(void) {
    prepare();
    playback_queue_item_t safety =
        make_interruptible("safety-2", PLAYBACK_PRIORITY_SAFETY, 1);
    playback_queue_item_t prompt =
        make_interruptible("prompt-2", PLAYBACK_PRIORITY_PROMPT, 1);
    assert(playback_queue_enqueue(&safety) == PLAYBACK_QUEUE_OK);
    assert(playback_queue_enqueue(&prompt) == PLAYBACK_QUEUE_OK);

    descriptor()->shutdown();
    assert(!playback_queue_is_ready());
    /* Shutdown force-drops everything, including pending safety audio. */
    assert(playback_queue_get_snapshot().pending_items == 0);

    playback_queue_host_reset();
    assert(descriptor()->initialize() == ESP_OK);
    assert(playback_queue_is_ready());
    descriptor()->shutdown();
}

static void test_module_descriptor_is_stable(void) {
    const module_descriptor_t *value = playback_queue_module_descriptor();
    assert(value != NULL);
    assert(strcmp(value->module_name, "playback_queue") == 0);
    assert(strcmp(value->version, "1.0.0") == 0);
    assert(value->initialize != NULL);
    assert(value->shutdown != NULL);
}

int main(void) {
    test_calls_before_init_are_refused();
    test_init_failures_are_reported();
    test_init_is_idempotent();
    test_enqueue_validates_the_item();
    test_enqueue_rejects_partial_frames();
    test_enqueue_accepts_a_full_item();
    test_duplicate_identifier_is_refused();
    test_safety_priority_is_protected_from_clear();
    test_queue_capacity_is_bounded();
    test_allocation_failure_is_reported_as_full();
    test_pause_and_resume_track_state();
    test_priority_and_error_names_are_stable();
    test_shutdown_clears_pending_items_and_allows_reinit();
    test_module_descriptor_is_stable();
    return 0;
}
