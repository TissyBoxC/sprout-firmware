#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_codec.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum identifier length including the null terminator. */
#define PLAYBACK_QUEUE_ITEM_ID_SIZE 48

/** @brief Number of audio frames one queue item may hold. */
#define PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM 32

/**
 * @brief Priority classes for outbound audio.
 *
 * Higher values preempt lower values. Safety announcements are never preempted
 * and are never dropped by mute, which mirrors the voice gateway playback queue
 * so both sides of the link agree on ordering.
 */
typedef enum {
    PLAYBACK_PRIORITY_AMBIENT = 0,
    PLAYBACK_PRIORITY_CONVERSATION,
    PLAYBACK_PRIORITY_PROMPT,
    PLAYBACK_PRIORITY_SAFETY,
    PLAYBACK_PRIORITY_COUNT,
} playback_priority_t;

/** @brief Stable errors returned by the queue. */
typedef enum {
    PLAYBACK_QUEUE_OK = 0,
    PLAYBACK_QUEUE_ERR_NOT_INITIALIZED,
    PLAYBACK_QUEUE_ERR_INVALID_ARGUMENT,
    PLAYBACK_QUEUE_ERR_FULL,
    PLAYBACK_QUEUE_ERR_DUPLICATE_ITEM,
    PLAYBACK_QUEUE_ERR_EMPTY_PAYLOAD,
    PLAYBACK_QUEUE_ERR_INTERRUPT_DENIED,
    PLAYBACK_QUEUE_ERR_NOT_FOUND,
} playback_queue_error_t;

/** @brief One queued audio segment made of whole 20 ms frames. */
typedef struct {
    char item_id[PLAYBACK_QUEUE_ITEM_ID_SIZE];
    playback_priority_t priority;
    bool is_interruptible;
    size_t frame_count;
    audio_codec_pcm_frame_t frames[PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM];
} playback_queue_item_t;

/** @brief Bounded queue state for telemetry and the device UI. */
typedef struct {
    playback_priority_t active_priority;
    bool has_active_item;
    bool is_paused;
    size_t pending_items;
    unsigned int dropped_items;
} playback_queue_snapshot_t;

/**
 * @brief Initialize the queue and its worker task.
 *
 * The worker blocks until an item is queued, so an idle queue consumes no CPU.
 * An item is only played while `audio_pipeline_is_playing` reports true.
 */
esp_err_t playback_queue_init(void);

/** @brief Return true when the queue is ready to accept items. */
bool playback_queue_is_ready(void);

/**
 * @brief Stop the worker, discard queued items, and release queue resources.
 *
 * Callers must stop producers before shutdown. The function waits for the
 * active frame submission to finish so the audio_output mixer is never used
 * after it has released its lanes.
 */
void playback_queue_shutdown(void);

/**
 * @brief Copy one item into the queue.
 *
 * The queue copies the frames, so the caller may reuse its buffer immediately.
 * A higher priority preempts a running lower priority item only when that item
 * is interruptible; otherwise the call fails with
 * PLAYBACK_QUEUE_ERR_INTERRUPT_DENIED so the caller can report a real conflict.
 */
playback_queue_error_t playback_queue_enqueue(
    const playback_queue_item_t *item
);

/** @brief Pause playback after the current frame. */
esp_err_t playback_queue_pause(void);

/** @brief Resume playback. */
esp_err_t playback_queue_resume(void);

/**
 * @brief Drop pending items.
 *
 * A generic clear keeps queued safety announcements, because a crisis prompt
 * must not be cancelled by an unrelated request. Pass include_safety to force a
 * full clear during session teardown.
 */
esp_err_t playback_queue_clear(bool include_safety);

/** @brief Return the bounded queue snapshot. */
playback_queue_snapshot_t playback_queue_get_snapshot(void);

/** @brief Return the stable string for one priority. */
const char *playback_priority_name(playback_priority_t priority);

/** @brief Return the stable string for one queue error code. */
const char *playback_queue_error_name(playback_queue_error_t error);

/** @brief Return the removable-module descriptor for playback_queue. */
const module_descriptor_t *playback_queue_module_descriptor(void);

#ifdef __cplusplus
}
#endif
