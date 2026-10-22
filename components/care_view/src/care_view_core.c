#include "care_view_core.h"

#include <string.h>

static care_view_snapshot_t g_snapshot;

void care_view_core_init(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.phase = CARE_VIEW_PHASE_IDLE;
}

care_view_snapshot_t care_view_core_snapshot(void)
{
    return g_snapshot;
}

bool care_view_core_is(care_view_phase_t phase)
{
    return g_snapshot.phase == phase;
}

bool care_view_core_allows_capture(void)
{
    /* This is the single place that answers "may the camera open?". A STARTING
     * session has accepted the request but has not confirmed the notice, and
     * capturing then would be a silent camera. */
    return g_snapshot.phase == CARE_VIEW_PHASE_VIEWING;
}

static void copy_bounded(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) {
        return;
    }
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

care_view_error_t care_view_core_start(const char *session_id,
                                       uint64_t now_ms,
                                       uint64_t deadline_ms,
                                       uint32_t max_seconds,
                                       bool indication_available)
{
    if (session_id == NULL || session_id[0] == '\0') {
        return CARE_VIEW_ERR_INVALID_ARGUMENT;
    }
    if (strlen(session_id) >= CARE_VIEW_SESSION_ID_MAX) {
        return CARE_VIEW_ERR_INVALID_ARGUMENT;
    }
    /* One viewer at a time. A new request must never preempt a running view:
     * the child agreed to be watched by someone, not by anyone. */
    if (g_snapshot.phase != CARE_VIEW_PHASE_IDLE) {
        return CARE_VIEW_ERR_BUSY;
    }
    /* Without a notice and a prompt this would be a covert camera. Refusing is
     * the only acceptable answer, and the caller reports camera_unavailable. */
    if (!indication_available) {
        return CARE_VIEW_ERR_NOT_READY;
    }
    if (max_seconds == 0) {
        return CARE_VIEW_ERR_INVALID_ARGUMENT;
    }

    /* Clamp the platform deadline to the device policy so a long or malformed
     * value cannot extend a view beyond what this build allows. */
    uint64_t clamps_to = now_ms + (uint64_t)max_seconds * 1000u;
    if (deadline_ms == 0 || deadline_ms > clamps_to) {
        deadline_ms = clamps_to;
    }
    /* A deadline already in the past would start and immediately stop. */
    if (deadline_ms <= now_ms) {
        return CARE_VIEW_ERR_EXPIRED;
    }

    memset(&g_snapshot, 0, sizeof(g_snapshot));
    copy_bounded(g_snapshot.session_id, sizeof(g_snapshot.session_id), session_id);
    g_snapshot.phase = CARE_VIEW_PHASE_STARTING;
    g_snapshot.deadline_ms = deadline_ms;
    /* The notice has been requested but is not yet confirmed on screen. */
    g_snapshot.indication_shown = false;
    return CARE_VIEW_OK;
}

care_view_error_t care_view_core_begin_viewing(uint64_t now_ms)
{
    if (g_snapshot.phase != CARE_VIEW_PHASE_STARTING) {
        return CARE_VIEW_ERR_NOT_VIEWING;
    }
    if (g_snapshot.deadline_ms != 0 && now_ms >= g_snapshot.deadline_ms) {
        /* The notice took so long that the window already closed. Starting now
         * would open a camera nobody is waiting for. */
        care_view_core_end(now_ms, CARE_VIEW_END_EXPIRED);
        return CARE_VIEW_ERR_EXPIRED;
    }
    g_snapshot.phase = CARE_VIEW_PHASE_VIEWING;
    g_snapshot.indication_shown = true;
    return CARE_VIEW_OK;
}

care_view_error_t care_view_core_end(uint64_t now_ms, care_view_end_reason_t reason)
{
    (void)now_ms;
    (void)reason;
    /* Ending twice is harmless: the runtime may learn about a stop from the
     * button, the command channel, or the uplink at the same instant. */
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.phase = CARE_VIEW_PHASE_IDLE;
    return CARE_VIEW_OK;
}

bool care_view_core_tick(uint64_t now_ms, care_view_end_reason_t *end_reason)
{
    if (g_snapshot.phase == CARE_VIEW_PHASE_IDLE) {
        return false;
    }
    if (g_snapshot.deadline_ms == 0 || now_ms < g_snapshot.deadline_ms) {
        return false;
    }
    if (end_reason != NULL) {
        *end_reason = CARE_VIEW_END_EXPIRED;
    }
    care_view_core_end(now_ms, CARE_VIEW_END_EXPIRED);
    return true;
}

void care_view_core_note_frame(bool dropped)
{
    if (dropped) {
        if (g_snapshot.frames_dropped < UINT32_MAX) {
            g_snapshot.frames_dropped++;
        }
        return;
    }
    if (g_snapshot.frames_sent < UINT32_MAX) {
        g_snapshot.frames_sent++;
    }
}

const char *care_view_phase_name(care_view_phase_t phase)
{
    switch (phase) {
    case CARE_VIEW_PHASE_IDLE:
        return "idle";
    case CARE_VIEW_PHASE_STARTING:
        return "starting";
    case CARE_VIEW_PHASE_VIEWING:
        return "viewing";
    default:
        return "unknown";
    }
}

const char *care_view_end_reason_name(care_view_end_reason_t reason)
{
    switch (reason) {
    case CARE_VIEW_END_GUARDIAN_CLOSED:
        return "guardian_closed";
    case CARE_VIEW_END_CHILD_CLOSED:
        return "child_closed";
    case CARE_VIEW_END_EXPIRED:
        return "expired";
    case CARE_VIEW_END_CAMERA_UNAVAILABLE:
        return "camera_unavailable";
    case CARE_VIEW_END_TRANSPORT_CLOSED:
        return "transport_closed";
    case CARE_VIEW_END_DECLINED:
        return "declined";
    default:
        return "unknown";
    }
}

const char *care_view_error_name(care_view_error_t error)
{
    switch (error) {
    case CARE_VIEW_OK:
        return "ok";
    case CARE_VIEW_ERR_INVALID_ARGUMENT:
        return "invalid_argument";
    case CARE_VIEW_ERR_NOT_READY:
        return "not_ready";
    case CARE_VIEW_ERR_BUSY:
        return "busy";
    case CARE_VIEW_ERR_NOT_VIEWING:
        return "not_viewing";
    case CARE_VIEW_ERR_EXPIRED:
        return "expired";
    default:
        return "unknown";
    }
}
