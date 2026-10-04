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

/** @brief Longest wake word name copied into a wake event, including NUL. */
#define VOICE_WAKE_WORD_NAME_SIZE 32

/** @brief Longest stable ASCII detail code copied into a wake event, including NUL. */
#define VOICE_WAKE_DETAIL_CODE_SIZE 64

/** @brief Fixed rate accepted from audio_pipeline capture. */
#define VOICE_WAKE_SAMPLE_RATE_HZ AUDIO_CODEC_SAMPLE_RATE_HZ

/** @brief Fixed frame duration accepted from audio_pipeline capture. */
#define VOICE_WAKE_FRAME_DURATION_MS AUDIO_CODEC_FRAME_DURATION_MS

/** @brief Samples in one accepted 16 kHz mono wake frame. */
#define VOICE_WAKE_FRAME_SAMPLES AUDIO_CODEC_SAMPLES_PER_FRAME

/** @brief Stable wake errors returned by lifecycle operations. */
typedef enum {
    VOICE_WAKE_ERROR_NONE = 0,
    VOICE_WAKE_ERROR_NOT_INITIALIZED,
    VOICE_WAKE_ERROR_INVALID_ARGUMENT,
    VOICE_WAKE_ERROR_INVALID_STATE,
    VOICE_WAKE_ERROR_NO_MEMORY,
    VOICE_WAKE_ERROR_BACKEND,
} voice_wake_error_t;

/**
 * @brief Bounded wake detection event delivered to the registered callback.
 *
 * The name is copied into this bounded buffer, so the callback may retain it.
 * The callback runs on the wake task and must return promptly.
 */
typedef struct {
    uint32_t wake_word_id;
    char wake_word_name[VOICE_WAKE_WORD_NAME_SIZE];
    char detail_code[VOICE_WAKE_DETAIL_CODE_SIZE];
    /** Confidence on a 0..1000 scale. */
    uint32_t confidence_milli;
    uint64_t detected_at_ms;
    uint32_t session_nonce;
} voice_wake_event_t;

/** @brief Callback invoked once for each accepted wake event. */
typedef void (*voice_wake_handler_t)(
    const voice_wake_event_t *event,
    void *context
);

/** @brief Bounded detector state and counters for diagnostics. */
typedef struct {
    bool is_ready;
    bool is_armed;
    bool is_suspended;
    uint32_t frames_processed;
    uint32_t detections;
    uint32_t rejected_frames;
    uint32_t false_wake_rejections;
    uint32_t last_confidence_milli;
    uint64_t last_detected_at_ms;
} voice_wake_snapshot_t;

/**
 * @brief Initialize the selected detector without enabling the microphone.
 *
 * Safe to call more than once. The module is left disarmed, capture stays off,
 * and the module only becomes ready after its backend initializes.
 */
esp_err_t voice_wake_init(void);

/** @brief Return true when the detector backend is initialized. */
bool voice_wake_is_ready(void);

/**
 * @brief Arm detection and enable audio_pipeline capture.
 *
 * Start and resume are the only paths that enable the microphone. Start
 * returns an error without enabling capture when the backend or pipeline is
 * not ready.
 */
esp_err_t voice_wake_start(void);

/** @brief Disarm detection and disable audio_pipeline capture. */
esp_err_t voice_wake_stop(void);

/**
 * @brief Suspend detection while keeping the armed state.
 *
 * This disables capture so a conversation can own the microphone without
 * self-triggering the detector. Resume re-enables capture only when armed.
 */
esp_err_t voice_wake_suspend(void);

/** @brief Resume a suspended detector only when it was armed. */
esp_err_t voice_wake_resume(void);

/**
 * @brief Register or clear the wake event callback.
 *
 * Passing NULL clears the handler. The handler runs on the wake task, so it
 * must copy any retained data and return quickly.
 */
esp_err_t voice_wake_set_wake_handler(
    voice_wake_handler_t handler,
    void *context
);

/** @brief Copy the current bounded detector snapshot. */
esp_err_t voice_wake_get_snapshot(voice_wake_snapshot_t *snapshot_out);

/** @brief Return the stable name for one wake error. */
const char *voice_wake_error_name(voice_wake_error_t error);

/** @brief Return the removable-module descriptor for voice_wake. */
const module_descriptor_t *voice_wake_module_descriptor(void);

#ifdef __cplusplus
}
#endif
