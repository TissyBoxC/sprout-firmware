#include "content_playback_chunker.h"

#include <stdio.h>
#include <string.h>

void content_playback_chunker_init(
    content_playback_chunker_t *chunker,
    size_t chunk_frame_limit,
    uint64_t total_frames
) {
    if (chunker == NULL) {
        return;
    }
    memset(chunker, 0, sizeof(*chunker));
    chunker->chunk_frame_limit =
        chunk_frame_limit > PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM
            ? PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM
            : chunk_frame_limit;
    if (chunker->chunk_frame_limit == 0) {
        chunker->chunk_frame_limit = 1;
    }
    chunker->total_frames = total_frames;
    chunker->next_sequence = 1;
}

bool content_playback_chunker_add(
    content_playback_chunker_t *chunker,
    const audio_codec_pcm_frame_t *frame,
    content_playback_chunk_t *chunk
) {
    if (chunker == NULL || frame == NULL || chunk == NULL ||
        chunk->frame_count >= chunker->chunk_frame_limit ||
        chunker->queued_frames >= chunker->total_frames) {
        return false;
    }
    chunk->frames[chunk->frame_count] = *frame;
    chunk->frames[chunk->frame_count].sequence = chunker->next_sequence;
    ++chunk->frame_count;
    ++chunker->next_sequence;
    ++chunker->queued_frames;
    if (chunk->frame_count == 1) {
        snprintf(
            chunk->item_id,
            sizeof(chunk->item_id),
            "content_%lu",
            (unsigned long)chunker->queued_frames
        );
    }
    return chunk->frame_count >= chunker->chunk_frame_limit;
}

bool content_playback_chunker_is_empty(
    const content_playback_chunker_t *chunker,
    const content_playback_chunk_t *chunk
) {
    (void)chunker;
    return chunk == NULL || chunk->frame_count == 0;
}

void content_playback_chunker_clear_chunk(content_playback_chunk_t *chunk) {
    if (chunk == NULL) {
        return;
    }
    memset(chunk, 0, sizeof(*chunk));
}

bool content_playback_chunker_is_complete(
    const content_playback_chunker_t *chunker
) {
    return chunker != NULL && chunker->queued_frames >= chunker->total_frames;
}
