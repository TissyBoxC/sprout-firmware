#include "usage_ledger_core.h"

#include <cassert>
#include <cstring>

static usage_ledger_state_t make_state(void) {
    usage_ledger_state_t state;
    usage_ledger_core_init_state(&state);
    return state;
}

static void test_state_version_round_trip(void) {
    usage_ledger_state_t state = make_state();
    assert(state.state_version == USAGE_LEDGER_STATE_VERSION);
    assert(usage_ledger_core_state_is_compatible(&state));

    // A stale or corrupted layout must be rejected so old bytes are not read
    // as valid usage.
    usage_ledger_state_t stale = state;
    stale.state_version = USAGE_LEDGER_STATE_VERSION - 1;
    assert(!usage_ledger_core_state_is_compatible(&stale));

    usage_ledger_state_t impossible = state;
    impossible.pending_count = USAGE_LEDGER_PENDING_CAPACITY + 1;
    assert(!usage_ledger_core_state_is_compatible(&impossible));
}

static void test_rollover_closes_previous_day(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_add_active_seconds(&state, 100, 480, 1200);
    assert(state.current_day.day_key == 100);
    assert(state.current_day.active_seconds == 1200);

    usage_ledger_core_roll_day_if_needed(&state, 101, 480);
    assert(state.pending_count == 1);
    usage_ledger_day_t pending = {};
    assert(usage_ledger_core_take_pending_day(&state, &pending));
    assert(pending.day_key == 100);
    assert(pending.active_seconds == 1200);
    assert(state.current_day.day_key == 101);
    assert(state.current_day.active_seconds == 0);
}

static void test_clock_rollback_does_not_corrupt_newer_day(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_add_active_seconds(&state, 200, 480, 600);
    usage_ledger_core_roll_day_if_needed(&state, 100, 480);
    assert(state.current_day.day_key == 200);
    assert(state.current_day.active_seconds == 600);
    assert(state.pending_count == 0);
}

static void test_monotonic_additions_accumulate(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_add_active_seconds(&state, 500, 480, 30);
    usage_ledger_core_add_active_seconds(&state, 500, 480, 45);
    assert(state.current_day.active_seconds == 75);
}

static void test_saturates_at_one_day(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_add_active_seconds(
        &state,
        600,
        480,
        USAGE_LEDGER_SECONDS_PER_DAY - 10
    );
    usage_ledger_core_add_active_seconds(&state, 600, 480, 100);
    assert(state.current_day.active_seconds == USAGE_LEDGER_SECONDS_PER_DAY);
}

static void test_conversation_and_content_totals(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_record_conversation(&state, 700, 480, 120);
    usage_ledger_core_record_conversation(&state, 700, 480, 90);
    usage_ledger_core_record_content_playback(
        &state,
        700,
        480,
        USAGE_LEDGER_CATEGORY_STORY,
        300
    );
    usage_ledger_core_record_content_playback(
        &state,
        700,
        480,
        USAGE_LEDGER_CATEGORY_STORY,
        60
    );
    assert(state.current_day.conversation_count == 2);
    assert(state.current_day.conversation_seconds == 210);
    assert(state.current_day.content_play_count == 2);
    assert(state.current_day.content_seconds == 360);
    assert(state.current_day.category_count == 1);
    assert(state.current_day.categories[0].category ==
           USAGE_LEDGER_CATEGORY_STORY);
    assert(state.current_day.categories[0].play_count == 2);
    assert(state.current_day.categories[0].seconds == 360);
}

static void test_blocked_counters(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_record_blocked(
        &state,
        800,
        480,
        "PARENT_CONTROL_REASON_DISABLED_PERIOD"
    );
    usage_ledger_core_record_blocked(
        &state,
        800,
        480,
        "PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED"
    );
    usage_ledger_core_record_blocked(
        &state,
        800,
        480,
        "PARENT_CONTROL_REASON_CATEGORY_DENIED"
    );
    usage_ledger_core_record_blocked(
        &state,
        800,
        480,
        "PARENT_CONTROL_REASON_TIME_UNTRUSTED"
    );
    assert(state.current_day.blocked.disabled_period == 1);
    assert(state.current_day.blocked.daily_limit == 1);
    assert(state.current_day.blocked.category_denied == 1);
    assert(state.current_day.blocked.time_untrusted == 1);
}

static void test_payload_matches_contract(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_record_conversation(&state, 900, 480, 60);
    usage_ledger_core_record_content_playback(
        &state,
        900,
        480,
        USAGE_LEDGER_CATEGORY_BEDTIME,
        180
    );
    usage_ledger_upload_payload_t payload = {};
    usage_ledger_core_build_payload(&state.current_day, &payload);
    assert(strcmp(payload.schema_version, "1.0.0") == 0);
    // Day key 900 is 900 days after the Unix epoch (1970-01-01).
    assert(strcmp(payload.report_date, "1972-06-19") == 0);
    assert(payload.timezone_offset_minutes == 480);
    assert(payload.conversation_count == 1);
    assert(payload.conversation_seconds == 60);
    assert(payload.content_play_count == 1);
    assert(payload.content_seconds == 180);
    assert(payload.category_count == 1);
    assert(payload.categories[0].category == USAGE_LEDGER_CATEGORY_BEDTIME);
}

static void test_pending_queue_is_oldest_first_and_bounded(void) {
    usage_ledger_state_t state = make_state();
    // Fill two more days than the queue holds so the oldest is evicted.
    for (int32_t day = 1; day <= (int32_t)USAGE_LEDGER_PENDING_CAPACITY + 2;
         ++day) {
        usage_ledger_core_add_active_seconds(
            &state,
            day,
            480,
            (uint32_t)day
        );
    }
    assert(state.pending_count == USAGE_LEDGER_PENDING_CAPACITY);
    assert(state.evicted_day_count == 1);
    usage_ledger_day_t pending = {};
    assert(usage_ledger_core_take_pending_day(&state, &pending));
    // Day 1 was evicted, so day 2 is now the oldest retained entry.
    assert(pending.day_key == 2);
}

static void test_multi_day_offline_uploads_in_order(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_add_active_seconds(&state, 10, 480, 100);
    usage_ledger_core_add_active_seconds(&state, 11, 480, 200);
    usage_ledger_core_add_active_seconds(&state, 12, 480, 300);
    assert(state.pending_count == 2);

    usage_ledger_day_t first = {};
    assert(usage_ledger_core_take_pending_day(&state, &first));
    assert(first.day_key == 10);
    assert(usage_ledger_core_confirm_pending_day(&state, 10));

    usage_ledger_day_t second = {};
    assert(usage_ledger_core_take_pending_day(&state, &second));
    assert(second.day_key == 11);
    assert(usage_ledger_core_confirm_pending_day(&state, 11));
    assert(state.pending_count == 0);

    // Confirming an unknown or already-confirmed day is a no-op.
    assert(!usage_ledger_core_confirm_pending_day(&state, 11));
}

static void test_idempotent_upload_after_restart(void) {
    usage_ledger_state_t state = make_state();
    usage_ledger_core_add_active_seconds(&state, 1000, 480, 300);
    usage_ledger_core_roll_day_if_needed(&state, 1001, 480);
    assert(state.pending_count == 1);

    // Simulate a reboot: persisted bytes round-trip into a fresh struct.
    usage_ledger_state_t restored = make_state();
    memcpy(&restored, &state, sizeof(restored));
    assert(usage_ledger_core_state_is_compatible(&restored));
    usage_ledger_day_t pending = {};
    assert(usage_ledger_core_take_pending_day(&restored, &pending));
    assert(pending.day_key == 1000);
    assert(pending.active_seconds == 300);

    // Confirming the same day twice is idempotent at the contract level.
    assert(usage_ledger_core_confirm_pending_day(&restored, 1000));
    assert(!usage_ledger_core_confirm_pending_day(&restored, 1000));
    assert(restored.pending_count == 0);
}

int main() {
    test_state_version_round_trip();
    test_rollover_closes_previous_day();
    test_clock_rollback_does_not_corrupt_newer_day();
    test_monotonic_additions_accumulate();
    test_saturates_at_one_day();
    test_conversation_and_content_totals();
    test_blocked_counters();
    test_payload_matches_contract();
    test_pending_queue_is_oldest_first_and_bounded();
    test_multi_day_offline_uploads_in_order();
    test_idempotent_upload_after_restart();
    return 0;
}
