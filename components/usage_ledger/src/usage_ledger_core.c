#include "usage_ledger_core.h"

#include <stdio.h>
#include <string.h>

static uint32_t usage_ledger_core_saturating_add(
    uint32_t current,
    uint32_t delta,
    uint32_t maximum
) {
    if (delta > maximum - current) {
        return maximum;
    }
    return current + delta;
}

static void usage_ledger_core_clear_day(usage_ledger_day_t *day) {
    memset(day, 0, sizeof(*day));
}

void usage_ledger_core_init_state(usage_ledger_state_t *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->state_version = USAGE_LEDGER_STATE_VERSION;
}

bool usage_ledger_core_state_is_compatible(
    const usage_ledger_state_t *state
) {
    if (state == NULL) {
        return false;
    }
    if (state->state_version != USAGE_LEDGER_STATE_VERSION) {
        return false;
    }
    return state->pending_count <= USAGE_LEDGER_PENDING_CAPACITY;
}

static void usage_ledger_core_start_day(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes
) {
    usage_ledger_core_clear_day(&state->current_day);
    state->current_day.day_key = day_key;
    state->current_day.timezone_offset_minutes = timezone_offset_minutes;
    state->has_current_day = true;
}

/**
 * @brief Enqueue one completed day for upload, oldest first.
 *
 * When the bounded queue is full the oldest day is evicted so the newest usage
 * survives. Evictions increment a persisted counter so data loss during a long
 * outage is observable rather than silent.
 */
static void usage_ledger_core_push_pending(
    usage_ledger_state_t *state,
    const usage_ledger_day_t *day
) {
    if (state->pending_count >= USAGE_LEDGER_PENDING_CAPACITY) {
        for (size_t index = 1; index < state->pending_count; ++index) {
            state->pending_days[index - 1] = state->pending_days[index];
        }
        --state->pending_count;
        ++state->evicted_day_count;
    }
    state->pending_days[state->pending_count] = *day;
    ++state->pending_count;
}

void usage_ledger_core_roll_day_if_needed(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes
) {
    if (state == NULL) {
        return;
    }
    if (!state->has_current_day) {
        usage_ledger_core_start_day(
            state,
            day_key,
            timezone_offset_minutes
        );
        return;
    }
    if (state->current_day.day_key == day_key) {
        state->current_day.timezone_offset_minutes = timezone_offset_minutes;
        return;
    }
    if (state->current_day.day_key > day_key) {
        // A wall-clock rollback must never overwrite a newer day. Keep the
        // existing record and let the caller surface time_untrusted instead.
        return;
    }

    usage_ledger_core_push_pending(state, &state->current_day);
    usage_ledger_core_start_day(
        state,
        day_key,
        timezone_offset_minutes
    );
}

bool usage_ledger_core_take_pending_day(
    usage_ledger_state_t *state,
    usage_ledger_day_t *day_out
) {
    if (state == NULL || day_out == NULL || state->pending_count == 0) {
        return false;
    }
    *day_out = state->pending_days[0];
    return true;
}

bool usage_ledger_core_confirm_pending_day(
    usage_ledger_state_t *state,
    int32_t day_key
) {
    if (state == NULL) {
        return false;
    }
    for (size_t index = 0; index < state->pending_count; ++index) {
        if (state->pending_days[index].day_key != day_key) {
            continue;
        }
        for (size_t shift = index + 1; shift < state->pending_count; ++shift) {
            state->pending_days[shift - 1] = state->pending_days[shift];
        }
        --state->pending_count;
        usage_ledger_core_clear_day(
            &state->pending_days[state->pending_count]
        );
        return true;
    }
    return false;
}

void usage_ledger_core_add_active_seconds(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    uint32_t monotonic_delta_seconds
) {
    if (state == NULL || monotonic_delta_seconds == 0) {
        return;
    }
    usage_ledger_core_roll_day_if_needed(
        state,
        day_key,
        timezone_offset_minutes
    );
    if (!state->has_current_day) {
        return;
    }
    state->current_day.active_seconds = usage_ledger_core_saturating_add(
        state->current_day.active_seconds,
        monotonic_delta_seconds,
        USAGE_LEDGER_SECONDS_PER_DAY
    );
}

void usage_ledger_core_record_conversation(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    uint32_t conversation_seconds
) {
    if (state == NULL) {
        return;
    }
    usage_ledger_core_roll_day_if_needed(
        state,
        day_key,
        timezone_offset_minutes
    );
    if (!state->has_current_day) {
        return;
    }
    state->current_day.conversation_count =
        usage_ledger_core_saturating_add(
            state->current_day.conversation_count,
            1,
            UINT32_MAX
        );
    state->current_day.conversation_seconds =
        usage_ledger_core_saturating_add(
            state->current_day.conversation_seconds,
            conversation_seconds,
            USAGE_LEDGER_SECONDS_PER_DAY
        );
}

static usage_ledger_category_usage_t *
usage_ledger_core_find_or_add_category(
    usage_ledger_day_t *day,
    usage_ledger_category_t category
) {
    for (size_t index = 0; index < day->category_count; ++index) {
        if (day->categories[index].category == category) {
            return &day->categories[index];
        }
    }
    if (day->category_count >= USAGE_LEDGER_MAX_CATEGORIES) {
        return NULL;
    }
    usage_ledger_category_usage_t *usage =
        &day->categories[day->category_count];
    memset(usage, 0, sizeof(*usage));
    usage->category = category;
    ++day->category_count;
    return usage;
}

void usage_ledger_core_record_content_playback(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    usage_ledger_category_t category,
    uint32_t content_seconds
) {
    if (state == NULL || category < USAGE_LEDGER_CATEGORY_STORY ||
        category >= USAGE_LEDGER_CATEGORY_COUNT) {
        return;
    }
    usage_ledger_core_roll_day_if_needed(
        state,
        day_key,
        timezone_offset_minutes
    );
    if (!state->has_current_day) {
        return;
    }
    state->current_day.content_play_count =
        usage_ledger_core_saturating_add(
            state->current_day.content_play_count,
            1,
            UINT32_MAX
        );
    state->current_day.content_seconds = usage_ledger_core_saturating_add(
        state->current_day.content_seconds,
        content_seconds,
        USAGE_LEDGER_SECONDS_PER_DAY
    );
    usage_ledger_category_usage_t *category_usage =
        usage_ledger_core_find_or_add_category(
            &state->current_day,
            category
        );
    if (category_usage == NULL) {
        return;
    }
    category_usage->play_count = usage_ledger_core_saturating_add(
        category_usage->play_count,
        1,
        UINT32_MAX
    );
    category_usage->seconds = usage_ledger_core_saturating_add(
        category_usage->seconds,
        content_seconds,
        USAGE_LEDGER_SECONDS_PER_DAY
    );
}

void usage_ledger_core_record_blocked(
    usage_ledger_state_t *state,
    int32_t day_key,
    int32_t timezone_offset_minutes,
    const char *blocked_reason
) {
    if (state == NULL || blocked_reason == NULL) {
        return;
    }
    usage_ledger_core_roll_day_if_needed(
        state,
        day_key,
        timezone_offset_minutes
    );
    if (!state->has_current_day) {
        return;
    }
    usage_ledger_blocked_counters_t *blocked =
        &state->current_day.blocked;
    if (strcmp(blocked_reason, "PARENT_CONTROL_REASON_DISABLED_PERIOD") == 0) {
        blocked->disabled_period = usage_ledger_core_saturating_add(
            blocked->disabled_period,
            1,
            UINT32_MAX
        );
    } else if (strcmp(
                   blocked_reason,
                   "PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED"
               ) == 0) {
        blocked->daily_limit = usage_ledger_core_saturating_add(
            blocked->daily_limit,
            1,
            UINT32_MAX
        );
    } else if (strcmp(
                   blocked_reason,
                   "PARENT_CONTROL_REASON_CATEGORY_DENIED"
               ) == 0) {
        blocked->category_denied = usage_ledger_core_saturating_add(
            blocked->category_denied,
            1,
            UINT32_MAX
        );
    } else if (strcmp(
                   blocked_reason,
                   "PARENT_CONTROL_REASON_TIME_UNTRUSTED"
               ) == 0) {
        blocked->time_untrusted = usage_ledger_core_saturating_add(
            blocked->time_untrusted,
            1,
            UINT32_MAX
        );
    }
}

void usage_ledger_core_build_payload(
    const usage_ledger_day_t *day,
    usage_ledger_upload_payload_t *payload_out
) {
    if (day == NULL || payload_out == NULL) {
        return;
    }
    memset(payload_out, 0, sizeof(*payload_out));
    payload_out->day_key = day->day_key;
    memcpy(
        payload_out->schema_version,
        USAGE_LEDGER_SCHEMA_VERSION,
        sizeof(USAGE_LEDGER_SCHEMA_VERSION)
    );
    (void)usage_ledger_core_format_report_date(
        day->day_key,
        payload_out->report_date
    );
    payload_out->timezone_offset_minutes = day->timezone_offset_minutes;
    payload_out->active_seconds = day->active_seconds;
    payload_out->conversation_count = day->conversation_count;
    payload_out->conversation_seconds = day->conversation_seconds;
    payload_out->content_play_count = day->content_play_count;
    payload_out->content_seconds = day->content_seconds;
    payload_out->category_count = day->category_count;
    if (payload_out->category_count > USAGE_LEDGER_MAX_CATEGORIES) {
        payload_out->category_count = USAGE_LEDGER_MAX_CATEGORIES;
    }
    memcpy(
        payload_out->categories,
        day->categories,
        payload_out->category_count * sizeof(day->categories[0])
    );
    payload_out->blocked = day->blocked;
}

bool usage_ledger_core_format_report_date(
    int32_t day_key,
    char output[USAGE_LEDGER_REPORT_DATE_SIZE]
) {
    if (output == NULL) {
        return false;
    }
    // Same civil-from-days algorithm as time_sync: the epoch is 0000-03-01, so
    // the Unix day number is shifted by the published 719468-day offset.
    int64_t days = (int64_t)day_key + 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t day_of_era = days - era * 146097;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
         day_of_era / 146096) /
        365;
    int64_t year = year_of_era + era * 400;
    const int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 -
                      year_of_era / 100);
    const int64_t month_prime = (5 * day_of_year + 2) / 153;
    const int64_t day =
        day_of_year - (153 * month_prime + 2) / 5 + 1;
    const int64_t month = month_prime < 10 ? month_prime + 3
                                           : month_prime - 9;
    year += month <= 2 ? 1 : 0;
    if (year < 0 || year > 9999) {
        output[0] = '\0';
        return false;
    }
    const int written = snprintf(
        output,
        USAGE_LEDGER_REPORT_DATE_SIZE,
        "%04d-%02d-%02d",
        (int)year,
        (int)month,
        (int)day
    );
    return written == 10;
}
