#include "prompt_tone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_codec.h"
#include "audio_pipeline.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_FEATURE_PLAYBACK_QUEUE
#include "playback_queue.h"
#endif
#if CONFIG_FEATURE_AUDIO_OUTPUT && __has_include("audio_output.h")
#include "audio_output.h"
#define PROMPT_TONE_HAS_AUDIO_OUTPUT 1
#else
#define PROMPT_TONE_HAS_AUDIO_OUTPUT 0
#endif

static const char *const TAG = "prompt_tone";

#define PROMPT_TONE_MAX_FRAMES 16

// One period of a sine wave at 64 samples. Keeping the table at 64 points lets
// the generator produce any tone by stepping through it at a variable rate,
// which avoids a floating-point sine call inside the 20 ms audio loop.
static const int16_t PROMPT_TONE_SINE_TABLE[64] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
    23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609,
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329,
    23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
    0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
    -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212,
};

/** One rendered cue: a sequence of frequency and duration segments. */
typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t amplitude_percent;
} prompt_tone_segment_t;

// Wake, capture, and error cues stay short so feedback never delays the
// conversation. The safety cue is deliberately distinct and repeated three
// times, because it must be recognisable without any on-screen explanation.
typedef struct {
    const prompt_tone_segment_t *segments;
    size_t segment_count;
} prompt_tone_definition_t;

static const prompt_tone_segment_t WAKE_ACCEPTED_SEGMENTS[] = {
    {880, 90, 45},
    {1320, 140, 45},
};
static const prompt_tone_segment_t CAPTURE_STARTED_SEGMENTS[] = {
    {660, 70, 35},
    {990, 70, 35},
};
static const prompt_tone_segment_t CAPTURE_STOPPED_SEGMENTS[] = {
    {990, 70, 35},
    {660, 70, 35},
};
static const prompt_tone_segment_t MICROPHONE_MUTED_SEGMENTS[] = {
    {440, 60, 40},
    {0, 60, 0},
    {440, 60, 40},
};
static const prompt_tone_segment_t VOLUME_LIMIT_SEGMENTS[] = {
    {520, 80, 40},
    {520, 40, 0},
    {520, 80, 40},
};
static const prompt_tone_segment_t NETWORK_LOST_SEGMENTS[] = {
    {392, 160, 45},
    {294, 220, 45},
};
static const prompt_tone_segment_t NETWORK_RESTORED_SEGMENTS[] = {
    {392, 90, 40},
    {587, 90, 40},
    {784, 140, 40},
};
static const prompt_tone_segment_t BATTERY_LOW_SEGMENTS[] = {
    {330, 120, 45},
    {247, 180, 45},
};
static const prompt_tone_segment_t SAFETY_ANNOUNCEMENT_SEGMENTS[] = {
    {1046, 180, 55},
    {0, 80, 0},
    {1046, 180, 55},
    {0, 80, 0},
    {1046, 260, 55},
};

static const prompt_tone_definition_t PROMPT_TONE_DEFINITIONS
    [PROMPT_TONE_COUNT] = {
        [PROMPT_TONE_WAKE_ACCEPTED] = {
            WAKE_ACCEPTED_SEGMENTS,
            sizeof(WAKE_ACCEPTED_SEGMENTS) / sizeof(WAKE_ACCEPTED_SEGMENTS[0]),
        },
        [PROMPT_TONE_CAPTURE_STARTED] = {
            CAPTURE_STARTED_SEGMENTS,
            sizeof(CAPTURE_STARTED_SEGMENTS) /
                sizeof(CAPTURE_STARTED_SEGMENTS[0]),
        },
        [PROMPT_TONE_CAPTURE_STOPPED] = {
            CAPTURE_STOPPED_SEGMENTS,
            sizeof(CAPTURE_STOPPED_SEGMENTS) /
                sizeof(CAPTURE_STOPPED_SEGMENTS[0]),
        },
        [PROMPT_TONE_MICROPHONE_MUTED] = {
            MICROPHONE_MUTED_SEGMENTS,
            sizeof(MICROPHONE_MUTED_SEGMENTS) /
                sizeof(MICROPHONE_MUTED_SEGMENTS[0]),
        },
        [PROMPT_TONE_VOLUME_LIMIT] = {
            VOLUME_LIMIT_SEGMENTS,
            sizeof(VOLUME_LIMIT_SEGMENTS) / sizeof(VOLUME_LIMIT_SEGMENTS[0]),
        },
        [PROMPT_TONE_NETWORK_LOST] = {
            NETWORK_LOST_SEGMENTS,
            sizeof(NETWORK_LOST_SEGMENTS) / sizeof(NETWORK_LOST_SEGMENTS[0]),
        },
        [PROMPT_TONE_NETWORK_RESTORED] = {
            NETWORK_RESTORED_SEGMENTS,
            sizeof(NETWORK_RESTORED_SEGMENTS) /
                sizeof(NETWORK_RESTORED_SEGMENTS[0]),
        },
        [PROMPT_TONE_BATTERY_LOW] = {
            BATTERY_LOW_SEGMENTS,
            sizeof(BATTERY_LOW_SEGMENTS) / sizeof(BATTERY_LOW_SEGMENTS[0]),
        },
        [PROMPT_TONE_SAFETY_ANNOUNCEMENT] = {
            SAFETY_ANNOUNCEMENT_SEGMENTS,
            sizeof(SAFETY_ANNOUNCEMENT_SEGMENTS) /
                sizeof(SAFETY_ANNOUNCEMENT_SEGMENTS[0]),
        },
};

static bool prompt_tone_ready;
static prompt_tone_snapshot_t prompt_tone_snapshot;

// The rendered frames stay in one shared buffer because a cue is at most ten
// kilobytes, which does not fit the caller's task stack. The mutex serialises
// callers so two tasks cannot overwrite each other's cue.
static audio_codec_pcm_frame_t prompt_tone_render_buffer[PROMPT_TONE_MAX_FRAMES];
static SemaphoreHandle_t prompt_tone_mutex;

static bool prompt_tone_is_valid(prompt_tone_t tone) {
    return tone >= PROMPT_TONE_WAKE_ACCEPTED && tone < PROMPT_TONE_COUNT;
}

/**
 * @brief Render one cue into a fixed frame buffer.
 *
 * Returns the number of whole 20 ms frames written. A trailing partial frame
 * is padded with silence rather than shortened, because the playback path only
 * accepts whole frames.
 */
static size_t prompt_tone_render(
    prompt_tone_t tone,
    audio_codec_pcm_frame_t *frames_out,
    size_t frame_capacity
) {
    const prompt_tone_definition_t definition = PROMPT_TONE_DEFINITIONS[tone];
    const size_t samples_per_frame = AUDIO_CODEC_SAMPLES_PER_FRAME;
    const size_t total_frames = frame_capacity < PROMPT_TONE_MAX_FRAMES
        ? frame_capacity
        : PROMPT_TONE_MAX_FRAMES;

    size_t frame_index = 0;
    size_t sample_index = 0;
    memset(frames_out, 0, total_frames * sizeof(audio_codec_pcm_frame_t));

    for (size_t segment_index = 0; segment_index < definition.segment_count;
         ++segment_index) {
        if (frame_index >= total_frames) {
            break;
        }
        const prompt_tone_segment_t segment = definition.segments[segment_index];
        const size_t segment_samples =
            ((size_t)segment.duration_ms * AUDIO_CODEC_SAMPLE_RATE_HZ) / 1000;
        const int32_t amplitude =
            ((int32_t)INT16_MAX * segment.amplitude_percent) / 100;
        // Phase advances in Q16 of one sine table period, so any frequency maps
        // onto the 64-entry table without a floating-point sample loop.
        const uint32_t phase_step = (uint32_t)(
            ((uint64_t)segment.frequency_hz * 64 * 65536) /
            AUDIO_CODEC_SAMPLE_RATE_HZ
        );
        uint32_t phase = 0;

        for (size_t index = 0; index < segment_samples; ++index) {
            if (frame_index >= total_frames) {
                break;
            }

            int16_t sample = 0;
            if (segment.frequency_hz != 0) {
                const size_t table_index = (phase >> 16) & 63U;
                sample = (int16_t)(
                    ((int32_t)PROMPT_TONE_SINE_TABLE[table_index] * amplitude)
                    / INT16_MAX
                );
                phase = (phase + phase_step) & ((64U << 16) - 1U);
            }
            frames_out[frame_index].pcm[sample_index] = sample;
            ++sample_index;
            if (sample_index == samples_per_frame) {
                sample_index = 0;
                ++frame_index;
            }
        }
    }

    // A cue that exactly fills its last frame must not add a silent frame.
    size_t rendered_frames = frame_index + (sample_index > 0 ? 1 : 0);
    for (size_t index = 0; index < rendered_frames; ++index) {
        frames_out[index].pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    }
    return rendered_frames;
}

#if CONFIG_FEATURE_PLAYBACK_QUEUE
static playback_priority_t prompt_tone_priority(prompt_tone_t tone) {
    if (tone == PROMPT_TONE_SAFETY_ANNOUNCEMENT) {
        return PLAYBACK_PRIORITY_SAFETY;
    }
    return PLAYBACK_PRIORITY_PROMPT;
}
#endif

#if PROMPT_TONE_HAS_AUDIO_OUTPUT
/**
 * @brief Render one cue into the mixer without disturbing the active reply.
 *
 * The cue is submitted frame by frame to its priority lane. A prompt therefore
 * rides over conversation audio, while a safety announcement uses the safety
 * lane and bypasses mute in audio_output.
 */
static prompt_tone_error_t prompt_tone_play_mixed(
    prompt_tone_t tone,
    size_t rendered_count
) {
    if (!audio_output_is_ready()) {
        return PROMPT_TONE_ERR_QUEUE;
    }

    const audio_output_priority_t priority =
        tone == PROMPT_TONE_SAFETY_ANNOUNCEMENT
            ? AUDIO_OUTPUT_PRIORITY_SAFETY
            : AUDIO_OUTPUT_PRIORITY_PROMPT;

    for (size_t index = 0; index < rendered_count; ++index) {
        const audio_output_error_t result = audio_output_submit(
            &prompt_tone_render_buffer[index],
            priority
        );
        if (result != AUDIO_OUTPUT_OK) {
            ESP_LOGW(
                TAG,
                "prompt mix submit failed: %s",
                audio_output_error_name(result)
            );
            // A cue is either heard in full or not at all. The lane is
            // serialized by prompt_tone_mutex, so no unrelated prompt is
            // discarded by this rollback.
            (void)audio_output_discard_priority(priority, NULL);
            return PROMPT_TONE_ERR_QUEUE;
        }
    }
    return PROMPT_TONE_OK;
}
#endif

esp_err_t prompt_tone_init(void) {
    if (prompt_tone_ready) {
        return ESP_OK;
    }
    if (prompt_tone_mutex == NULL) {
        prompt_tone_mutex = xSemaphoreCreateMutex();
        if (prompt_tone_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    prompt_tone_snapshot = (prompt_tone_snapshot_t){0};
    prompt_tone_ready = true;
    return ESP_OK;
}

bool prompt_tone_is_ready(void) {
    return prompt_tone_ready;
}

prompt_tone_error_t prompt_tone_play(prompt_tone_t tone) {
    if (!prompt_tone_ready) {
        return PROMPT_TONE_ERR_NOT_INITIALIZED;
    }
    if (!prompt_tone_is_valid(tone)) {
        ++prompt_tone_snapshot.tones_rejected;
        return PROMPT_TONE_ERR_INVALID_TONE;
    }
    if (xSemaphoreTake(prompt_tone_mutex, portMAX_DELAY) != pdTRUE) {
        return PROMPT_TONE_ERR_QUEUE;
    }

    const size_t rendered_count = prompt_tone_render(
        tone,
        prompt_tone_render_buffer,
        PROMPT_TONE_MAX_FRAMES
    );
    if (rendered_count == 0) {
        ++prompt_tone_snapshot.tones_rejected;
        xSemaphoreGive(prompt_tone_mutex);
        return PROMPT_TONE_ERR_INVALID_TONE;
    }

#if PROMPT_TONE_HAS_AUDIO_OUTPUT
    {
        const prompt_tone_error_t mixed_result = prompt_tone_play_mixed(
            tone,
            rendered_count
        );
        if (mixed_result == PROMPT_TONE_OK) {
            ++prompt_tone_snapshot.tones_played;
            if (tone == PROMPT_TONE_SAFETY_ANNOUNCEMENT) {
                ++prompt_tone_snapshot.safety_tones_played;
            }
            xSemaphoreGive(prompt_tone_mutex);
            return PROMPT_TONE_OK;
        }
        bool can_fallback_to_queue = false;
#if CONFIG_FEATURE_PLAYBACK_QUEUE
        can_fallback_to_queue = playback_queue_is_ready();
#endif
        if (!can_fallback_to_queue) {
            ++prompt_tone_snapshot.tones_rejected;
            xSemaphoreGive(prompt_tone_mutex);
            return mixed_result;
        }
    }
#endif

#if CONFIG_FEATURE_PLAYBACK_QUEUE
    if (playback_queue_is_ready()) {
        // One queue item holds up to thirty-two 640-byte frames, so it is far
        // larger than any task stack and must live on the heap.
        playback_queue_item_t *item = heap_caps_calloc_prefer(
            1,
            sizeof(playback_queue_item_t),
            2,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            MALLOC_CAP_8BIT
        );
        if (item == NULL) {
            ++prompt_tone_snapshot.tones_rejected;
            xSemaphoreGive(prompt_tone_mutex);
            return PROMPT_TONE_ERR_QUEUE;
        }
        const int written = snprintf(
            item->item_id,
            sizeof(item->item_id),
            "prompt_%u_%u",
            (unsigned)tone,
            (unsigned)prompt_tone_snapshot.tones_played
        );
        if (written <= 0 || (size_t)written >= sizeof(item->item_id)) {
            ++prompt_tone_snapshot.tones_rejected;
            free(item);
            xSemaphoreGive(prompt_tone_mutex);
            return PROMPT_TONE_ERR_QUEUE;
        }
        item->priority = prompt_tone_priority(tone);
        // A safety announcement is by definition not interruptible, and the
        // queue enforces that again when it copies the item.
        item->is_interruptible = tone != PROMPT_TONE_SAFETY_ANNOUNCEMENT;
        item->frame_count = rendered_count;
        memcpy(
            item->frames,
            prompt_tone_render_buffer,
            rendered_count * sizeof(audio_codec_pcm_frame_t)
        );
        const playback_queue_error_t result = playback_queue_enqueue(item);
        free(item);
        if (result != PLAYBACK_QUEUE_OK) {
            ++prompt_tone_snapshot.tones_rejected;
            ESP_LOGW(
                TAG,
                "prompt enqueue failed: %s",
                playback_queue_error_name(result)
            );
            xSemaphoreGive(prompt_tone_mutex);
            return PROMPT_TONE_ERR_QUEUE;
        }
        ++prompt_tone_snapshot.tones_played;
        if (tone == PROMPT_TONE_SAFETY_ANNOUNCEMENT) {
            ++prompt_tone_snapshot.safety_tones_played;
        }
        xSemaphoreGive(prompt_tone_mutex);
        return PROMPT_TONE_OK;
    }
#endif

    // Without the playback queue the cue is rendered straight to the speaker,
    // which keeps wake and error feedback working in a minimal build.
    if (!audio_pipeline_is_ready()) {
        ++prompt_tone_snapshot.tones_rejected;
        xSemaphoreGive(prompt_tone_mutex);
        return PROMPT_TONE_ERR_QUEUE;
    }
    if (audio_pipeline_set_playing(true) != ESP_OK) {
        ++prompt_tone_snapshot.tones_rejected;
        xSemaphoreGive(prompt_tone_mutex);
        return PROMPT_TONE_ERR_QUEUE;
    }
    for (size_t index = 0; index < rendered_count; ++index) {
        if (audio_pipeline_play_frame(&prompt_tone_render_buffer[index]) !=
            AUDIO_CODEC_OK) {
            ++prompt_tone_snapshot.tones_rejected;
            xSemaphoreGive(prompt_tone_mutex);
            return PROMPT_TONE_ERR_QUEUE;
        }
    }
    ++prompt_tone_snapshot.tones_played;
    if (tone == PROMPT_TONE_SAFETY_ANNOUNCEMENT) {
        ++prompt_tone_snapshot.safety_tones_played;
    }
    xSemaphoreGive(prompt_tone_mutex);
    return PROMPT_TONE_OK;
}

prompt_tone_snapshot_t prompt_tone_get_snapshot(void) {
    return prompt_tone_snapshot;
}

const char *prompt_tone_name(prompt_tone_t tone) {
    switch (tone) {
        case PROMPT_TONE_WAKE_ACCEPTED:
            return "wake_accepted";
        case PROMPT_TONE_CAPTURE_STARTED:
            return "capture_started";
        case PROMPT_TONE_CAPTURE_STOPPED:
            return "capture_stopped";
        case PROMPT_TONE_MICROPHONE_MUTED:
            return "microphone_muted";
        case PROMPT_TONE_VOLUME_LIMIT:
            return "volume_limit";
        case PROMPT_TONE_NETWORK_LOST:
            return "network_lost";
        case PROMPT_TONE_NETWORK_RESTORED:
            return "network_restored";
        case PROMPT_TONE_BATTERY_LOW:
            return "battery_low";
        case PROMPT_TONE_SAFETY_ANNOUNCEMENT:
            return "safety_announcement";
        default:
            return "unknown";
    }
}

const char *prompt_tone_error_name(prompt_tone_error_t error) {
    switch (error) {
        case PROMPT_TONE_OK:
            return "ok";
        case PROMPT_TONE_ERR_NOT_INITIALIZED:
            return "audio_codec_not_initialized";
        case PROMPT_TONE_ERR_INVALID_TONE:
            return "audio_codec_invalid_argument";
        case PROMPT_TONE_ERR_QUEUE:
        default:
            return "audio_queue_full";
    }
}

const module_descriptor_t *prompt_tone_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "prompt_tone",
        .version = "1.0.0",
        .initialize = prompt_tone_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
