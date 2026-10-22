// Host tests for the pure care-view state machine. This machine is the only
// authority that can report the camera as allowed to capture, so these cases pin
// the child-safety contract directly rather than through the runtime.

#include <cstdio>
#include <cstring>

#include "care_view_core.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

constexpr uint64_t kSecond = 1000u;

void test_idle_allows_nothing() {
    care_view_core_init();
    check(!care_view_core_allows_capture(),
          "an idle device must not allow capture");
    check(care_view_core_snapshot().phase == CARE_VIEW_PHASE_IDLE,
          "init leaves the machine idle");
}

void test_start_requires_indication() {
    care_view_core_init();
    // No notice or prompt means no camera. This is the covert-camera guard.
    care_view_error_t error =
        care_view_core_start("session-1", 1000, 60000, 180, false);
    check(error == CARE_VIEW_ERR_NOT_READY,
          "start without a notice or prompt must be refused");
    check(!care_view_core_allows_capture(),
          "a refused start must not open the camera");
}

void test_start_does_not_capture_immediately() {
    care_view_core_init();
    care_view_error_t error =
        care_view_core_start("session-1", 1000, 60000, 180, true);
    check(error == CARE_VIEW_OK, "start with indication succeeds");
    check(care_view_core_snapshot().phase == CARE_VIEW_PHASE_STARTING,
          "start enters the starting phase");
    // The notice has been requested but the screen is not confirmed yet, so
    // capturing now would be a silent camera for one frame.
    check(!care_view_core_allows_capture(),
          "starting must not allow capture before the notice is confirmed");
}

void test_begin_viewing_allows_capture() {
    care_view_core_init();
    care_view_core_start("session-1", 1000, 60000, 180, true);
    care_view_error_t error = care_view_core_begin_viewing(1500);
    check(error == CARE_VIEW_OK, "begin viewing succeeds from starting");
    check(care_view_core_allows_capture(), "viewing allows capture");
    check(care_view_core_snapshot().indication_shown,
          "viewing records that the indication was shown");
}

void test_begin_viewing_requires_starting() {
    care_view_core_init();
    // Nothing was accepted, so there is nothing to confirm.
    check(care_view_core_begin_viewing(1000) == CARE_VIEW_ERR_NOT_VIEWING,
          "begin viewing from idle is rejected");
}

void test_busy_rejects_preemption() {
    care_view_core_init();
    care_view_core_start("session-1", 1000, 60000, 180, true);
    care_view_core_begin_viewing(1200);

    // A second guardian must never take over a view the child is already in.
    care_view_error_t error =
        care_view_core_start("session-2", 2000, 60000, 180, true);
    check(error == CARE_VIEW_ERR_BUSY, "a running view must not be preempted");

    care_view_snapshot_t snapshot = care_view_core_snapshot();
    check(std::strcmp(snapshot.session_id, "session-1") == 0,
          "the original session keeps the device");
    check(care_view_core_allows_capture(),
          "the original view keeps capturing");
}

void test_deadline_is_clamped_to_policy() {
    care_view_core_init();
    // The platform asks for an hour; device policy allows 180 seconds.
    care_view_core_start("session-1", 0, 3600 * kSecond, 180, true);
    check(care_view_core_snapshot().deadline_ms == 180 * kSecond,
          "the platform deadline is clamped to the device policy");
}

void test_tick_expires_and_stops_capture() {
    care_view_core_init();
    care_view_core_start("session-1", 0, 5000, 180, true);
    care_view_core_begin_viewing(100);
    check(care_view_core_allows_capture(), "capturing before the deadline");

    care_view_end_reason_t reason = CARE_VIEW_END_GUARDIAN_CLOSED;
    check(!care_view_core_tick(4000, &reason), "no expiry before the deadline");
    check(care_view_core_allows_capture(), "still capturing before the deadline");

    // The device enforces its own deadline: a lost stop command must not leave
    // the camera running.
    check(care_view_core_tick(5000, &reason), "the deadline expires the session");
    check(reason == CARE_VIEW_END_EXPIRED, "expiry reports the expired reason");
    check(!care_view_core_allows_capture(), "expiry stops capture");
    check(care_view_core_snapshot().phase == CARE_VIEW_PHASE_IDLE,
          "expiry returns the machine to idle");
}

void test_tick_is_noop_when_idle() {
    care_view_core_init();
    care_view_end_reason_t reason = CARE_VIEW_END_GUARDIAN_CLOSED;
    check(!care_view_core_tick(999999, &reason), "an idle tick does nothing");
}

void test_end_is_idempotent() {
    care_view_core_init();
    care_view_core_start("session-1", 0, 5000, 180, true);
    care_view_core_begin_viewing(100);

    // The button, the command channel, and the uplink can all report a stop at
    // the same instant, so ending twice must be harmless.
    check(care_view_core_end(200, CARE_VIEW_END_CHILD_CLOSED) == CARE_VIEW_OK,
          "first end succeeds");
    check(care_view_core_end(210, CARE_VIEW_END_CHILD_CLOSED) == CARE_VIEW_OK,
          "second end is harmless");
    check(!care_view_core_allows_capture(), "an ended view does not capture");
}

void test_expired_start_is_rejected() {
    care_view_core_init();
    // A deadline that already passed would open a camera nobody is waiting for.
    care_view_error_t error =
        care_view_core_start("session-1", 10000, 9000, 180, true);
    check(error == CARE_VIEW_ERR_EXPIRED, "a past deadline is refused");
    check(!care_view_core_allows_capture(), "a refused start does not capture");
}

void test_viewing_after_deadline_does_not_start() {
    care_view_core_init();
    care_view_core_start("session-1", 0, 1000, 180, true);
    // The notice took longer than the window.
    care_view_error_t error = care_view_core_begin_viewing(2000);
    check(error == CARE_VIEW_ERR_EXPIRED,
          "begin viewing after the deadline is refused");
    check(!care_view_core_allows_capture(),
          "a view that missed its window never captures");
}

void test_invalid_session_id_is_rejected() {
    care_view_core_init();
    check(care_view_core_start(nullptr, 0, 5000, 180, true) ==
              CARE_VIEW_ERR_INVALID_ARGUMENT,
          "a null session id is rejected");
    check(care_view_core_start("", 0, 5000, 180, true) ==
              CARE_VIEW_ERR_INVALID_ARGUMENT,
          "an empty session id is rejected");

    char oversized[CARE_VIEW_SESSION_ID_MAX + 8];
    std::memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    check(care_view_core_start(oversized, 0, 5000, 180, true) ==
              CARE_VIEW_ERR_INVALID_ARGUMENT,
          "an oversized session id is rejected");
}

void test_second_session_allowed_after_end() {
    care_view_core_init();
    care_view_core_start("session-1", 0, 5000, 180, true);
    care_view_core_begin_viewing(100);
    care_view_core_end(200, CARE_VIEW_END_GUARDIAN_CLOSED);

    // Ending must release the device so the next view can start.
    check(care_view_core_start("session-2", 300, 5000, 180, true) == CARE_VIEW_OK,
          "a new session starts after the previous one ended");
    check(std::strcmp(care_view_core_snapshot().session_id, "session-2") == 0,
          "the new session owns the device");
}

void test_frame_counters() {
    care_view_core_init();
    care_view_core_start("session-1", 0, 5000, 180, true);
    care_view_core_begin_viewing(100);

    care_view_core_note_frame(false);
    care_view_core_note_frame(false);
    care_view_core_note_frame(true);

    care_view_snapshot_t snapshot = care_view_core_snapshot();
    check(snapshot.frames_sent == 2, "sent frames are counted");
    check(snapshot.frames_dropped == 1, "dropped frames are counted");
}

void test_start_clears_previous_counters() {
    care_view_core_init();
    care_view_core_start("session-1", 0, 5000, 180, true);
    care_view_core_begin_viewing(100);
    care_view_core_note_frame(false);
    care_view_core_end(200, CARE_VIEW_END_GUARDIAN_CLOSED);

    care_view_core_start("session-2", 300, 5000, 180, true);
    // Counters belong to one session; carrying them over would misreport the
    // second view's traffic.
    check(care_view_core_snapshot().frames_sent == 0,
          "a new session starts with zero sent frames");
}

void test_stable_names() {
    check(std::strcmp(care_view_phase_name(CARE_VIEW_PHASE_VIEWING), "viewing") == 0,
          "phase names are stable");
    check(std::strcmp(care_view_end_reason_name(CARE_VIEW_END_CHILD_CLOSED),
                      "child_closed") == 0,
          "end reason names are stable");
    check(std::strcmp(care_view_error_name(CARE_VIEW_ERR_BUSY), "busy") == 0,
          "error names are stable");
}

}  // namespace

int main() {
    test_idle_allows_nothing();
    test_start_requires_indication();
    test_start_does_not_capture_immediately();
    test_begin_viewing_allows_capture();
    test_begin_viewing_requires_starting();
    test_busy_rejects_preemption();
    test_deadline_is_clamped_to_policy();
    test_tick_expires_and_stops_capture();
    test_tick_is_noop_when_idle();
    test_end_is_idempotent();
    test_expired_start_is_rejected();
    test_viewing_after_deadline_does_not_start();
    test_invalid_session_id_is_rejected();
    test_second_session_allowed_after_end();
    test_frame_counters();
    test_start_clears_previous_counters();
    test_stable_names();

    if (failures == 0) {
        std::printf("care_view core tests passed\n");
        return 0;
    }
    std::printf("care_view core tests failed: %d\n", failures);
    return 1;
}
