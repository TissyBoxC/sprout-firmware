#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_downloader.h"
#include "playback_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bounded chunk of PCM frames. */
typedef struct {
    char item_id[PLAYBACK_QUEUE_ITEM_ID_SIZE];
    size_t frame_count;
    audio_codec_pcm_frame_t frames[PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM];
} content_playback_chunk_t;

/** @brief Incremental chunk builder for one package. */
typedef struct {
    uint32_t next_sequence;
    uint64_t queued_frames;
    uint64_t total_frames;
    size_t chunk_frame_limit;
} content_playback_chunker_t;

/** @brief Prepare a chunker for one package. */
void content_playback_chunker_init(
    content_playback_chunker_t *chunker,
    size_t chunk_frame_limit,
    uint64_t total_frames
);

/**
 * @brief Add one decoded frame to the current chunk.
 *
 * Returns true when the chunk reached its frame limit and must be submitted.
 * The frame is copied; the caller may reuse its buffer immediately.
 */
bool content_playback_chunker_add(
    content_playback_chunker_t *chunker,
    const audio_codec_pcm_frame_t *frame,
    content_playback_chunk_t *chunk
);

/** @brief Return true when the chunk has no frames. */
bool content_playback_chunker_is_empty(
    const content_playback_chunker_t *chunker,
    const content_playback_chunk_t *chunk
);

/** @brief Reset the current chunk frame count after submission. */
void content_playback_chunker_clear_chunk(content_playback_chunk_t *chunk);

/** @brief Return true when every expected frame has been queued. */
bool content_playback_chunker_is_complete(
    const content_playback_chunker_t *chunker
);

#ifdef __cplusplus
}
#endif
