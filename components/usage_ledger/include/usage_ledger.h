#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Contract version accepted by the platform usage upload endpoint. */
#define USAGE_LEDGER_SCHEMA_VERSION "1.0.0"

/** @brief Persisted layout version; a mismatch discards the cached blob. */
#define USAGE_LEDGER_STATE_VERSION 2u

/** @brief Maximum per-category counters retained for one local day. */
#define USAGE_LEDGER_MAX_CATEGORIES 16

/** @brief Maximum category token length including the terminator. */
#define USAGE_LEDGER_CATEGORY_SIZE 32

/** @brief Longest ISO-8601 report date copied into a payload. */
#define USAGE_LEDGER_REPORT_DATE_SIZE 11

/**
 * @brief Maximum completed days retained for offline upload.
 *
 * Sixteen days covers a long network outage. When the queue is full the oldest
 * day is evicted so the newest usage is always preserved; evictions are
 * surfaced through the persisted eviction counter rather than hidden.
 */
#define USAGE_LEDGER_PENDING_CAPACITY 16

/**
 * @brief Minimum seconds between durable counter writes.
 *
 * Counter updates accumulate in RAM and are flushed at most this often, on day
 * rollover, and on shutdown. A power loss can therefore lose at most this many
 * seconds of active time instead of wearing out flash with one commit per
 * second.
 */
#define USAGE_LEDGER_FLUSH_INTERVAL_SECONDS 30

/** @brief Stable category identifiers accepted from content packages. */
typedef enum {
    USAGE_LEDGER_CATEGORY_STORY = 0,
    USAGE_LEDGER_CATEGORY_NURSERY_RHYME,
    USAGE_LEDGER_CATEGORY_POETRY,
    USAGE_LEDGER_CATEGORY_ENGLISH,
    USAGE_LEDGER_CATEGORY_ENCYCLOPEDIA,
    USAGE_LEDGER_CATEGORY_BEDTIME,
    USAGE_LEDGER_CATEGORY_COUNT,
} usage_ledger_category_t;

/** @brief Blocked-attempt counters required by the usage upload contract. */
typedef struct {
    uint32_t disabled_period;
    uint32_t daily_limit;
    uint32_t category_denied;
    uint32_t time_untrusted;
} usage_ledger_blocked_counters_t;

/** @brief Per-category play and duration totals for one local day. */
typedef struct {
    usage_ledger_category_t category;
    uint32_t play_count;
    uint32_t seconds;
} usage_ledger_category_usage_t;

/** @brief Complete local usage record for one local calendar day. */
typedef struct {
    int32_t day_key;
    int32_t timezone_offset_minutes;
    uint32_t active_seconds;
    uint32_t conversation_count;
    uint32_t conversation_seconds;
    uint32_t content_play_count;
    uint32_t content_seconds;
    usage_ledger_category_usage_t
        categories[USAGE_LEDGER_MAX_CATEGORIES];
    size_t category_count;
    usage_ledger_blocked_counters_t blocked;
    int64_t last_policy_version;
} usage_ledger_day_t;

/** @brief Ledger head plus the bounded pending-upload queue. */
typedef struct {
    uint32_t state_version;
    usage_ledger_day_t current_day;
    bool has_current_day;
    uint32_t pending_count;
    usage_ledger_day_t pending_days[USAGE_LEDGER_PENDING_CAPACITY];
    uint32_t evicted_day_count;
} usage_ledger_state_t;

/** @brief Bounded upload payload matching device_usage_upload.schema.json. */
typedef struct {
    /**
     * @brief Local day key of this payload; used to confirm the upload.
     *
     * Not part of the wire contract; the platform identifies the day by
     * report_date. Kept here so the caller confirms the exact day it sent
     * instead of re-reading the queue head.
     */
    int32_t day_key;
    char schema_version[8];
    char report_date[USAGE_LEDGER_REPORT_DATE_SIZE];
    int32_t timezone_offset_minutes;
    uint32_t active_seconds;
    uint32_t conversation_count;
    uint32_t conversation_seconds;
    uint32_t content_play_count;
    uint32_t content_seconds;
    usage_ledger_category_usage_t
        categories[USAGE_LEDGER_MAX_CATEGORIES];
    size_t category_count;
    usage_ledger_blocked_counters_t blocked;
} usage_ledger_upload_payload_t;

/** @brief Initialize the persisted ledger and restore pending state. */
esp_err_t usage_ledger_init(void);

/** @brief Return true when the ledger can record or upload data. */
bool usage_ledger_is_ready(void);

/**
 * @brief Add active device seconds to the current local day.
 *
 * Uses the caller-supplied monotonic delta, not wall-clock subtraction, so a
 * clock rollback cannot shrink or corrupt accumulated usage. Crossing local
 * midnight closes the previous day and marks it pending for upload.
 */
esp_err_t usage_ledger_add_active_seconds(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    uint32_t monotonic_delta_seconds
);

/**
 * @brief Start or stop an activity that consumes daily allowance.
 *
 * When active, the ledger accumulates from monotonic time rather than
 * subtracting wall-clock timestamps, so a clock correction cannot create or
 * erase usage. The first active tick establishes the local day.
 */
esp_err_t usage_ledger_set_active(
    bool active,
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes
);

/** @brief Record one completed conversation and its elapsed seconds. */
esp_err_t usage_ledger_record_conversation(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    uint32_t conversation_seconds
);

/** @brief Record one completed content playback. */
esp_err_t usage_ledger_record_content_playback(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    const char *category,
    uint32_t content_seconds
);

/** @brief Increment one stable blocked-attempt counter. */
esp_err_t usage_ledger_record_blocked(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    const char *blocked_reason
);

/** @brief Persist the most recently applied parent-policy revision. */
esp_err_t usage_ledger_set_applied_policy_version(int64_t policy_version);

/** @brief Return the most recently applied parent-policy revision, or -1. */
int64_t usage_ledger_get_applied_policy_version(void);

/** @brief Return the number of completed days dropped because the queue filled. */
uint32_t usage_ledger_evicted_day_count(void);

/** @brief Copy the current day record. */
esp_err_t usage_ledger_get_current_day(usage_ledger_day_t *day_out);

/**
 * @brief Return the number of completed days awaiting upload.
 */
uint32_t usage_ledger_pending_day_count(void);

/**
 * @brief Copy the oldest pending upload day record.
 *
 * Oldest-first ordering keeps offline backfill deterministic: the platform
 * receives days in the order they completed. Returns ESP_ERR_NOT_FOUND when no
 * day is pending.
 */
esp_err_t usage_ledger_get_pending_day(usage_ledger_day_t *day_out);

/** @brief Return remaining seconds for one daily limit, saturating at zero. */
uint32_t usage_ledger_remaining_seconds(uint32_t daily_limit_minutes);

/** @brief Return true when the current day has reached the daily limit. */
bool usage_ledger_daily_limit_reached(uint32_t daily_limit_minutes);

/**
 * @brief Build the pending upload payload for one device-local day.
 *
 * The caller must not free the payload; it is owned by the ledger until the
 * platform confirms or a newer day replaces it.
 */
esp_err_t usage_ledger_get_pending_upload(
    usage_ledger_upload_payload_t *payload_out
);

/** @brief Mark the pending upload confirmed and clear it durably. */
esp_err_t usage_ledger_mark_upload_confirmed(int32_t day_key);

/**
 * @brief Persist the in-memory ledger immediately.
 *
 * Counter mutations are flushed on a throttle; call this on shutdown and from
 * a periodic maintenance path so a power loss loses as little as possible.
 */
esp_err_t usage_ledger_flush(void);

/**
 * @brief Close the current day and mark it pending.
 *
 * Called on midnight rollover and during shutdown so a power cycle at the
 * boundary cannot lose the completed day.
 */
esp_err_t usage_ledger_close_current_day(void);

/** @brief Return the stable text for one category or NULL when unknown. */
const char *usage_ledger_category_name(usage_ledger_category_t category);

/** @brief Resolve one category contract token; false when unknown. */
bool usage_ledger_category_from_name(
    const char *category_name,
    usage_ledger_category_t *category_out
);

/** @brief Release runtime resources; persisted counters remain on flash. */
void usage_ledger_shutdown(void);

/** @brief Return the removable-module descriptor for usage_ledger. */
const module_descriptor_t *usage_ledger_module_descriptor(void);

#ifdef __cplusplus
}
#endif
