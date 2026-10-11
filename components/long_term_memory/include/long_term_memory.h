#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of long-term memory entries kept on the device. */
#define LONG_TERM_MEMORY_CAPACITY 16

/** @brief Size of a memory entry id including the terminator. */
#define LONG_TERM_MEMORY_ID_SIZE 40

/** @brief Size of a child id including the terminator. */
#define LONG_TERM_MEMORY_CHILD_ID_SIZE 40

/** @brief Size of a memory key including the terminator. */
#define LONG_TERM_MEMORY_KEY_SIZE 32

/** @brief Size of a memory value including the terminator. */
#define LONG_TERM_MEMORY_VALUE_SIZE 160

/**
 * @brief One guardian-authorized long-term memory entry.
 *
 * Memory is keyed by a stable key so re-learning one fact replaces the old
 * value. Nothing is stored without an explicit guardian authorization; a
 * revoked authorization erases every entry on the next read.
 */
typedef struct {
    char entry_id[LONG_TERM_MEMORY_ID_SIZE];
    char child_id[LONG_TERM_MEMORY_CHILD_ID_SIZE];
    char key[LONG_TERM_MEMORY_KEY_SIZE];
    char value[LONG_TERM_MEMORY_VALUE_SIZE];
    int64_t updated_at_epoch_seconds;
} long_term_memory_entry_t;

/** @brief Stable long-term-memory error codes for diagnostics and UI copy. */
typedef enum {
    LONG_TERM_MEMORY_OK = 0,
    LONG_TERM_MEMORY_ERR_NOT_INITIALIZED,
    LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT,
    LONG_TERM_MEMORY_ERR_FULL,
    LONG_TERM_MEMORY_ERR_NOT_FOUND,
    LONG_TERM_MEMORY_ERR_STORAGE,
    LONG_TERM_MEMORY_ERR_CONSENT_REQUIRED,
} long_term_memory_error_t;

/** @brief Initialize the long-term memory store from the configuration store. */
esp_err_t long_term_memory_init(void);

/** @brief Return true when the memory store is ready. */
bool long_term_memory_is_ready(void);

/**
 * @brief Set the guardian authorization for long-term memory.
 *
 * Revoking authorization erases every stored entry so an unauthorized memory
 * can never be read back.
 */
esp_err_t long_term_memory_set_authorized(bool authorized);

/** @brief Return true when the guardian has authorized long-term memory. */
bool long_term_memory_is_authorized(void);

/** @brief Insert or replace one entry; replaced entries keep their position. */
esp_err_t long_term_memory_remember(const long_term_memory_entry_t *entry);

/** @brief Remove one entry by id. A missing entry is success. */
esp_err_t long_term_memory_forget(const char *entry_id);

/** @brief Remove every entry for one child. Returns the removed count. */
esp_err_t long_term_memory_forget_child(
    const char *child_id,
    size_t *removed_out
);

/** @brief Look up one entry by key for one child. */
bool long_term_memory_recall(
    const char *child_id,
    const char *key,
    long_term_memory_entry_t *entry_out
);

/**
 * @brief Copy the stored entries.
 *
 * When long-term memory is unauthorized this reports zero entries so no
 * memory is ever surfaced without consent.
 */
esp_err_t long_term_memory_list(
    long_term_memory_entry_t *entries_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return the current number of stored entries. */
size_t long_term_memory_count(void);

/** @brief Return the stable string for one long-term-memory error code. */
const char *long_term_memory_error_name(long_term_memory_error_t error);

/** @brief Return the removable-module descriptor for long_term_memory. */
const module_descriptor_t *long_term_memory_module_descriptor(void);

#ifdef __cplusplus
}
#endif
