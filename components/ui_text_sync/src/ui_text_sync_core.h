#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One published device_firmware package from a revision page. */
typedef struct {
    char package_id[UI_TEXT_PACKAGE_ID_SIZE];
    char language[UI_TEXT_LANGUAGE_SIZE];
    uint32_t package_version;
    int64_t revision;
} ui_text_sync_candidate_t;

/** @brief One withdrawal from a revision page. */
typedef struct {
    char package_id[UI_TEXT_PACKAGE_ID_SIZE];
    char language[UI_TEXT_LANGUAGE_SIZE];
    int64_t revision;
} ui_text_sync_withdrawal_t;

/** @brief The single action a revision page implies for the device. */
typedef enum {
    UI_TEXT_SYNC_PLAN_NONE = 0,
    UI_TEXT_SYNC_PLAN_APPLY,
    UI_TEXT_SYNC_PLAN_WITHDRAW,
} ui_text_sync_plan_t;

/**
 * @brief Decide what one revision page changes for the active package.
 *
 * Only candidates that match the configured language are eligible. When any
 * eligible publication is present the newest one (highest revision, then
 * highest package version) is selected for apply, because a fresh publication
 * always supersedes a withdrawal of the previous copy. A withdrawal is only
 * honoured when it targets the currently active package id in the same
 * language, so a withdrawal for a package the device never applied is a no-op.
 *
 * @param[in] candidates Parsed device_firmware publications, or NULL.
 * @param[in] candidate_count Number of publication entries.
 * @param[in] withdrawals Parsed withdrawals, or NULL.
 * @param[in] withdrawal_count Number of withdrawal entries.
 * @param[in] language Device language to match; empty selects nothing.
 * @param[in] active_package_id Active package id, or NULL/empty when none.
 * @param[out] selected_out Receives the chosen publication on APPLY; ignored
 *                          otherwise. May be NULL.
 *
 * @return The action the caller must take for this page.
 */
ui_text_sync_plan_t ui_text_sync_core_plan(
    const ui_text_sync_candidate_t *candidates,
    size_t candidate_count,
    const ui_text_sync_withdrawal_t *withdrawals,
    size_t withdrawal_count,
    const char *language,
    const char *active_package_id,
    ui_text_sync_candidate_t *selected_out
);

#ifdef __cplusplus
}
#endif
