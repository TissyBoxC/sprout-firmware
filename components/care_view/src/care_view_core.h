#ifndef CARE_VIEW_CORE_H
#define CARE_VIEW_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure care-view state machine.
 *
 * This file has no ESP-IDF dependency beyond the error type so the rules can be
 * compiled and executed on the host. The runtime (care_view.cpp) owns hardware
 * and the network; this file owns the decisions.
 *
 * The rule that shapes everything here: a view must never start capturing
 * unless the device can show a visible notice AND play an audible prompt. If
 * either is unavailable the view is refused rather than started silently.
 */

#define CARE_VIEW_SESSION_ID_MAX 40
#define CARE_VIEW_REASON_MAX 32

typedef enum {
    CARE_VIEW_PHASE_IDLE = 0,
    /* Accepted and about to start: the notice and prompt are being shown. */
    CARE_VIEW_PHASE_STARTING,
    /* Capturing and sending frames. This is the only phase that sends. */
    CARE_VIEW_PHASE_VIEWING,
} care_view_phase_t;

typedef enum {
    CARE_VIEW_OK = 0,
    CARE_VIEW_ERR_INVALID_ARGUMENT,
    CARE_VIEW_ERR_NOT_READY,
    CARE_VIEW_ERR_BUSY,
    CARE_VIEW_ERR_NOT_VIEWING,
    CARE_VIEW_ERR_EXPIRED,
} care_view_error_t;

typedef enum {
    /* The view ran and was stopped by the guardian, the deadline, or the child. */
    CARE_VIEW_END_GUARDIAN_CLOSED = 0,
    /* The child pressed the device button to stop being watched. */
    CARE_VIEW_END_CHILD_CLOSED,
    /* The device's own deadline fired. */
    CARE_VIEW_END_EXPIRED,
    /* The camera could not start or broke mid-view. */
    CARE_VIEW_END_CAMERA_UNAVAILABLE,
    /* The uplink closed underneath the view. */
    CARE_VIEW_END_TRANSPORT_CLOSED,
    /* The device refused before capturing anything. */
    CARE_VIEW_END_DECLINED,
} care_view_end_reason_t;

typedef struct {
    care_view_phase_t phase;
    /* Session currently owned by the device, empty when idle. */
    char session_id[CARE_VIEW_SESSION_ID_MAX];
    /* Monotonic deadline in milliseconds. Zero when idle. */
    uint64_t deadline_ms;
    /* Frames handed to the uplink over the life of the session. */
    uint32_t frames_sent;
    /* Frames dropped for size or rate limits. */
    uint32_t frames_dropped;
    /* Whether the notice and prompt were confirmed before capture began. */
    bool indication_shown;
} care_view_snapshot_t;

/* Reset the machine to idle. */
void care_view_core_init(void);

/* Return the current snapshot. */
care_view_snapshot_t care_view_core_snapshot(void);

/*
 * Accept a start request.
 *
 * @param session_id   Platform session identifier, non-empty, bounded length.
 * @param now_ms       Current monotonic milliseconds.
 * @param deadline_ms  Requested deadline in the same clock.
 * @param max_seconds  Device policy ceiling; the deadline is clamped to it.
 * @param indication_available  Whether a visible notice and audible prompt can
 *                              be shown. When false the request is refused and
 *                              the caller must report camera_unavailable.
 *
 * Returns CARE_VIEW_ERR_BUSY while another session is active: a device serves
 * one viewer at a time and must never be preempted.
 */
care_view_error_t care_view_core_start(const char *session_id,
                                       uint64_t now_ms,
                                       uint64_t deadline_ms,
                                       uint32_t max_seconds,
                                       bool indication_available);

/* Move from STARTING to VIEWING once the indication is confirmed on screen. */
care_view_error_t care_view_core_begin_viewing(uint64_t now_ms);

/* End the active session with a reason. Idempotent when already idle. */
care_view_error_t care_view_core_end(uint64_t now_ms, care_view_end_reason_t reason);

/*
 * Advance the clock against the session deadline.
 *
 * Returns true when this call expired the session, writing the reason the
 * runtime must report. Returns false while the session is still running or when
 * nothing was active. The device enforces this itself rather than trusting the
 * platform, so a lost stop command cannot leave a camera running.
 */
bool care_view_core_tick(uint64_t now_ms, care_view_end_reason_t *end_reason);

/* Report whether frames may be captured right now. */
bool care_view_core_allows_capture(void);

/* Count one sent or dropped frame. */
void care_view_core_note_frame(bool dropped);

/* Return true when the phase is the given value; used by the runtime. */
bool care_view_core_is(care_view_phase_t phase);

/* Stable names for logging and command acknowledgements. */
const char *care_view_phase_name(care_view_phase_t phase);
const char *care_view_end_reason_name(care_view_end_reason_t reason);
const char *care_view_error_name(care_view_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* CARE_VIEW_CORE_H */
