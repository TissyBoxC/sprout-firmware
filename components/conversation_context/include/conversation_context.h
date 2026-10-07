#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of turn summaries retained per conversation.
 *
 * The gateway owns the authoritative multi-turn LLM history. The device keeps
 * a small bounded window so it can label a session, show the recent turns, and
 * build an offline fallback prompt without unbounded memory growth.
 */
#define CONVERSATION_CONTEXT_CAPACITY 8

/** @brief Maximum stored summary length excluding the terminator. */
#define CONVERSATION_CONTEXT_SUMMARY_SIZE 128

/** @brief Maximum stored role length including the terminator. */
#define CONVERSATION_CONTEXT_ROLE_SIZE 16

/** @brief Stable errors returned by the conversation context module. */
typedef enum {
    CONVERSATION_CONTEXT_OK = 0,
    CONVERSATION_CONTEXT_ERR_NOT_INITIALIZED,
    CONVERSATION_CONTEXT_ERR_INVALID_ARGUMENT,
    CONVERSATION_CONTEXT_ERR_INVALID_STATE,
    CONVERSATION_CONTEXT_ERR_NO_MEMORY,
} conversation_context_error_t;

/** @brief One bounded turn summary in the recent conversation window. */
typedef struct {
    char role[CONVERSATION_CONTEXT_ROLE_SIZE];
    char summary[CONVERSATION_CONTEXT_SUMMARY_SIZE + 1];
    uint32_t sequence;
    uint64_t created_at_ms;
} conversation_context_turn_t;

/** @brief Bounded counters describing the current window. */
typedef struct {
    bool is_initialized;
    uint32_t turn_count;
    uint32_t total_appended;
    uint32_t evicted;
    uint32_t next_sequence;
} conversation_context_snapshot_t;

/**
 * @brief Initialize the conversation context window.
 *
 * Idempotent. Clears any retained turns so a factory reset or a removed
 * conversation cannot leak prior child dialogue into a new session.
 */
esp_err_t conversation_context_init(void);

/** @brief Return true when the window is ready for appends. */
bool conversation_context_is_ready(void);

/**
 * @brief Append one turn summary, evicting the oldest when the window is full.
 *
 * The summary is copied into a bounded buffer and truncated to
 * CONVERSATION_CONTEXT_SUMMARY_SIZE. An empty summary is rejected so an
 * accidental call cannot insert a meaningless placeholder turn.
 */
esp_err_t conversation_context_append(
    const char *role,
    const char *summary
);

/**
 * @brief Copy the most recent turns in chronological order.
 *
 * Returns up to max_turns entries ending at the newest turn. The caller owns
 * the output array and must provide a size for at least one entry. On success
 * turn_count_out holds the number of written entries.
 */
esp_err_t conversation_context_get_recent(
    conversation_context_turn_t *turns_out,
    size_t max_turns,
    size_t *turn_count_out
);

/** @brief Discard every retained turn while keeping the module initialized. */
esp_err_t conversation_context_clear(void);

/** @brief Copy the bounded window counters. */
esp_err_t conversation_context_get_snapshot(
    conversation_context_snapshot_t *snapshot_out
);

/** @brief Release resources; the module returns to the uninitialized state. */
void conversation_context_shutdown(void);

/** @brief Return the removable-module descriptor for conversation_context. */
const module_descriptor_t *conversation_context_module_descriptor(void);

#ifdef __cplusplus
}
#endif
