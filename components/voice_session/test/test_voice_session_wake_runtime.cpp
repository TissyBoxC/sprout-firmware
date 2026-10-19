#include "voice_session_host_config.h"
#include "voice_session.h"
#include "voice_session_wake_observer.h"

#include <cassert>
#include <cstdint>
#include <cstring>

extern "C" {
void voice_session_host_poll_wake_once(void);
void voice_session_host_set_start_session_handler(
    esp_err_t (*handler)(const char *stream_id, const char *session_id)
);
uint32_t voice_session_host_wake_observation_sequence(void);

void voice_session_host_reset_runtime(void);
bool voice_session_host_has_pending_wake(void);
void voice_session_host_set_active_session(bool active);

void voice_session_test_set_wake_snapshot(
    const voice_wake_snapshot_t *snapshot
);
void voice_session_test_reset_queues(void);
}

static voice_wake_snapshot_t host_snapshot;
static uint32_t host_start_calls;
static esp_err_t host_start_result;

static esp_err_t start_handler(
    const char *stream_id,
    const char *session_id
) {
    (void)stream_id;
    (void)session_id;
    host_start_calls++;
    return host_start_result;
}

static void reset_runtime(void) {
    voice_session_host_reset_runtime();
    voice_session_test_reset_queues();
    std::memset(&host_snapshot, 0, sizeof(host_snapshot));
    host_start_calls = 0;
    host_start_result = ESP_OK;
    voice_session_host_set_start_session_handler(start_handler);
    voice_session_test_set_wake_snapshot(&host_snapshot);
}

static void establish_baseline(void) {
    voice_session_host_poll_wake_once();
}

static void set_snapshot(
    uint32_t sequence,
    uint32_t wake_word_id,
    const char *name,
    uint32_t confidence_milli
) {
    host_snapshot.observation_sequence = sequence;
    host_snapshot.latest_event.wake_word_id = wake_word_id;
    host_snapshot.latest_event.confidence_milli = confidence_milli;
    if (name != nullptr) {
        std::strncpy(
            host_snapshot.latest_event.wake_word_name,
            name,
            sizeof(host_snapshot.latest_event.wake_word_name) - 1
        );
    }
    voice_session_test_set_wake_snapshot(&host_snapshot);
}

static void test_first_real_detection_starts_session_once(void) {
    reset_runtime();
    establish_baseline();
    assert(host_start_calls == 0);

    set_snapshot(2, 5, "ni_hao_xiao_zhi", 920);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
    assert(voice_session_host_has_pending_wake());
}

static void test_duplicate_snapshot_does_not_restart_session(void) {
    reset_runtime();
    establish_baseline();
    set_snapshot(1, 5, "ni_hao_xiao_zhi", 920);
    voice_session_host_poll_wake_once();
    voice_session_host_poll_wake_once();
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
}

static void test_multiple_missed_detections_coalesce_to_one_start(void) {
    reset_runtime();
    establish_baseline();
    set_snapshot(1, 5, "ni_hao_xiao_zhi", 920);
    voice_session_host_poll_wake_once();
    // The first detection is staged for the new session. Let the normal
    // handshake path consume it before presenting a later observation.
    voice_session_host_set_active_session(true);
    set_snapshot(2, 8, "latest_wake", 800);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
}

static void test_invalid_name_fails_closed(void) {
    reset_runtime();
    establish_baseline();
    set_snapshot(1, 5, "", 920);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 0);
}

static void test_start_failure_leaves_no_pending_wake(void) {
    reset_runtime();
    establish_baseline();
    host_start_result = ESP_ERR_INVALID_STATE;
    set_snapshot(1, 5, "ni_hao_xiao_zhi", 920);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
    assert(!voice_session_host_has_pending_wake());
}

static void test_active_session_sends_wake_without_restart(void) {
    reset_runtime();
    establish_baseline();
    voice_session_host_set_active_session(true);
    set_snapshot(1, 5, "ni_hao_xiao_zhi", 920);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 0);
    // The wake frame is queued for the sender; this seam verifies that the
    // active-session branch does not request another session start.
}

static void test_detection_before_first_poll_is_not_lost(void) {
    reset_runtime();
    // The detector can observe a wake before the sender task's first poll.
    // Initialization must retain that observation rather than treating it as
    // the baseline.
    set_snapshot(4, 8, "latest_wake", 830);
    establish_baseline();
    assert(voice_session_host_wake_observation_sequence() == 4);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
    assert(voice_session_host_has_pending_wake());
}

static void test_reinitialization_starts_from_a_fresh_baseline(void) {
    reset_runtime();
    establish_baseline();
    set_snapshot(3, 8, "latest_wake", 830);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
    voice_session_host_reset_runtime();
    voice_session_test_reset_queues();
    voice_session_host_set_start_session_handler(start_handler);
    // The queued observation from the previous runtime must not survive a
    // reset; the next comparison starts from the current empty snapshot.
    assert(voice_session_host_wake_observation_sequence() == 0);
    host_start_calls = 0;
    voice_session_host_set_active_session(false);
    set_snapshot(4, 9, "next_wake", 910);
    voice_session_host_poll_wake_once();
    assert(host_start_calls == 1);
}

int main() {
    test_first_real_detection_starts_session_once();
    test_duplicate_snapshot_does_not_restart_session();
    test_multiple_missed_detections_coalesce_to_one_start();
    test_invalid_name_fails_closed();
    test_start_failure_leaves_no_pending_wake();
    test_active_session_sends_wake_without_restart();
    test_detection_before_first_poll_is_not_lost();
    test_reinitialization_starts_from_a_fresh_baseline();
    return 0;
}
