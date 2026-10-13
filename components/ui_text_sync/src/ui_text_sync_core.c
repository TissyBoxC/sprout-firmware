#include "ui_text_sync_core.h"

#include <string.h>

static bool ui_text_sync_has_text(const char *value) {
    return value != NULL && value[0] != '\0';
}

// Returns true when the candidate is newer than the running best. Revision is
// authoritative because the platform bumps it on every publication; the
// package version breaks ties so a re-published higher version still wins.
static bool ui_text_sync_is_newer(
    const ui_text_sync_candidate_t *candidate,
    const ui_text_sync_candidate_t *current
) {
    if (candidate->revision != current->revision) {
        return candidate->revision > current->revision;
    }
    return candidate->package_version > current->package_version;
}

ui_text_sync_plan_t ui_text_sync_core_plan(
    const ui_text_sync_candidate_t *candidates,
    size_t candidate_count,
    const ui_text_sync_withdrawal_t *withdrawals,
    size_t withdrawal_count,
    const char *language,
    const char *active_package_id,
    ui_text_sync_candidate_t *selected_out
) {
    if (!ui_text_sync_has_text(language)) {
        return UI_TEXT_SYNC_PLAN_NONE;
    }

    const ui_text_sync_candidate_t *best = NULL;
    for (size_t index = 0; index < candidate_count; ++index) {
        const ui_text_sync_candidate_t *candidate = &candidates[index];
        if (!ui_text_sync_has_text(candidate->package_id) ||
            candidate->package_version == 0 ||
            strcmp(candidate->language, language) != 0) {
            continue;
        }
        if (best == NULL || ui_text_sync_is_newer(candidate, best)) {
            best = candidate;
        }
    }

    if (best != NULL) {
        if (selected_out != NULL) {
            *selected_out = *best;
        }
        return UI_TEXT_SYNC_PLAN_APPLY;
    }

    if (!ui_text_sync_has_text(active_package_id)) {
        return UI_TEXT_SYNC_PLAN_NONE;
    }
    for (size_t index = 0; index < withdrawal_count; ++index) {
        const ui_text_sync_withdrawal_t *withdrawal = &withdrawals[index];
        if (!ui_text_sync_has_text(withdrawal->package_id) ||
            strcmp(withdrawal->language, language) != 0) {
            continue;
        }
        if (strcmp(withdrawal->package_id, active_package_id) == 0) {
            return UI_TEXT_SYNC_PLAN_WITHDRAW;
        }
    }
    return UI_TEXT_SYNC_PLAN_NONE;
}
