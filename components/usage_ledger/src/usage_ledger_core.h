#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usage_ledger.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Seconds in one local calendar day. */
#define USAGE_LEDGER_SECONDS_PER_DAY 86400u

/** @brief Prepare an empty ledger with the current layout version set. */
void usage_ledger_core_init_state(usage_ledger_state_t *state);

/** @brief Return true when a restored state matches the current layout. */
bool usage_ledger_core_state_is_compatible(
    const usage_ledger_state_t *state
);

/** @brief Apply a monotonic delta to the correct local day. */
void usage_ledger_core_add_active_seconds(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    uint32_t monotonic_delta_seconds
);

/** @brief Apply one conversation to the correct local day. */
void usage_ledger_core_record_conversation(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    uint32_t conversation_seconds
);

/** @brief Apply one category playback to the correct local day. */
void usage_ledger_core_record_content_playback(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    usage_ledger_category_t category,
    uint32_t content_seconds
);

/** @brief Apply one blocked counter to the correct local day. */
void usage_ledger_core_record_blocked(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    const char *blocked_reason
);

/** @brief Roll the current day when the supplied local day key changes. */
void usage_ledger_core_roll_day_if_needed(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes
);

/** @brief Copy the oldest pending day without removing it. */
bool usage_ledger_core_take_pending_day(
    usage_ledger_state_t *state,
    usage_ledger_day_t *day_out
);

/** @brief Remove one confirmed pending day by key; false when absent. */
bool usage_ledger_core_confirm_pending_day(
    usage_ledger_state_t *state,
    int32_t day_key
);

/** @brief Build a bounded upload payload from one day record. */
void usage_ledger_core_build_payload(
    const usage_ledger_day_t *day,
    usage_ledger_upload_payload_t *payload_out
);

/** @brief Convert a day key back to civil YYYY-MM-DD. */
bool usage_ledger_core_format_report_date(
    int32_t day_key,
    char output[USAGE_LEDGER_REPORT_DATE_SIZE]
);

#ifdef __cplusplus
}
#endif
