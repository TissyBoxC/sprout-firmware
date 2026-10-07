#include "content_playback_chunker.h"

#include <cassert>

int main() {
    content_playback_chunker_t chunker = {};
    content_playback_chunker_init(&chunker, 3, 7);

    content_playback_chunk_t chunk = {};
    audio_codec_pcm_frame_t frame = {};
    assert(!content_playback_chunker_add(&chunker, &frame, &chunk));
    assert(!content_playback_chunker_add(&chunker, &frame, &chunk));
    assert(content_playback_chunker_add(&chunker, &frame, &chunk));
    assert(chunk.frame_count == 3);
    assert(chunk.frames[0].sequence == 1);
    assert(chunk.frames[2].sequence == 3);

    content_playback_chunker_clear_chunk(&chunk);
    assert(content_playback_chunker_is_empty(&chunker, &chunk));
    assert(!content_playback_chunker_is_complete(&chunker));

    for (int index = 0; index < 4; ++index) {
        content_playback_chunker_add(&chunker, &frame, &chunk);
    }
    assert(chunk.frame_count == 3);
    assert(!content_playback_chunker_is_complete(&chunker));
    content_playback_chunker_clear_chunk(&chunk);
    content_playback_chunker_add(&chunker, &frame, &chunk);
    assert(chunk.frame_count == 1);
    assert(content_playback_chunker_is_complete(&chunker));
    return 0;
}
