#include "ui_text_sync_core.h"

#include <cassert>
#include <cstring>

static ui_text_sync_candidate_t make_candidate(
    const char *package_id,
    const char *language,
    uint32_t version,
    int64_t revision
) {
    ui_text_sync_candidate_t candidate = {};
    std::strncpy(
        candidate.package_id,
        package_id,
        sizeof(candidate.package_id) - 1
    );
    std::strncpy(candidate.language, language, sizeof(candidate.language) - 1);
    candidate.package_version = version;
    candidate.revision = revision;
    return candidate;
}

static ui_text_sync_withdrawal_t make_withdrawal(
    const char *package_id,
    const char *language,
    int64_t revision
) {
    ui_text_sync_withdrawal_t withdrawal = {};
    std::strncpy(
        withdrawal.package_id,
        package_id,
        sizeof(withdrawal.package_id) - 1
    );
    std::strncpy(
        withdrawal.language,
        language,
        sizeof(withdrawal.language) - 1
    );
    withdrawal.revision = revision;
    return withdrawal;
}

int main() {
    ui_text_sync_candidate_t selected = {};

    // An empty page changes nothing.
    assert(
        ui_text_sync_core_plan(
            NULL,
            0,
            NULL,
            0,
            "zh-CN",
            NULL,
            &selected
        ) == UI_TEXT_SYNC_PLAN_NONE
    );

    // A publication in the device language is selected for apply.
    ui_text_sync_candidate_t single[1] = {
        make_candidate("home_welcome", "zh-CN", 2, 10),
    };
    assert(
        ui_text_sync_core_plan(
            single,
            1,
            NULL,
            0,
            "zh-CN",
            NULL,
            &selected
        ) == UI_TEXT_SYNC_PLAN_APPLY
    );
    assert(std::strcmp(selected.package_id, "home_welcome") == 0);
    assert(selected.package_version == 2);

    // A publication in another language is ignored.
    ui_text_sync_candidate_t other_language[1] = {
        make_candidate("home_welcome", "en-US", 3, 11),
    };
    assert(
        ui_text_sync_core_plan(
            other_language,
            1,
            NULL,
            0,
            "zh-CN",
            NULL,
            &selected
        ) == UI_TEXT_SYNC_PLAN_NONE
    );

    // The newest revision wins, regardless of array order.
    ui_text_sync_candidate_t many[3] = {
        make_candidate("home_welcome", "zh-CN", 3, 20),
        make_candidate("home_welcome", "zh-CN", 5, 18),
        make_candidate("home_welcome", "zh-CN", 4, 30),
    };
    assert(
        ui_text_sync_core_plan(
            many,
            3,
            NULL,
            0,
            "zh-CN",
            NULL,
            &selected
        ) == UI_TEXT_SYNC_PLAN_APPLY
    );
    assert(selected.package_version == 4);
    assert(selected.revision == 30);

    // A fresh publication supersedes a withdrawal of the same page.
    ui_text_sync_withdrawal_t replaced[1] = {
        make_withdrawal("home_welcome", "zh-CN", 31),
    };
    assert(
        ui_text_sync_core_plan(
            many,
            3,
            replaced,
            1,
            "zh-CN",
            "home_welcome",
            &selected
        ) == UI_TEXT_SYNC_PLAN_APPLY
    );

    // A withdrawal of the active package reverts to built-in text.
    assert(
        ui_text_sync_core_plan(
            NULL,
            0,
            replaced,
            1,
            "zh-CN",
            "home_welcome",
            &selected
        ) == UI_TEXT_SYNC_PLAN_WITHDRAW
    );

    // A withdrawal of a package the device never applied is a no-op.
    assert(
        ui_text_sync_core_plan(
            NULL,
            0,
            replaced,
            1,
            "zh-CN",
            "home_other",
            &selected
        ) == UI_TEXT_SYNC_PLAN_NONE
    );

    // A withdrawal with no active package is a no-op.
    assert(
        ui_text_sync_core_plan(
            NULL,
            0,
            replaced,
            1,
            "zh-CN",
            NULL,
            &selected
        ) == UI_TEXT_SYNC_PLAN_NONE
    );

    // A withdrawal in another language is ignored.
    ui_text_sync_withdrawal_t other_language_withdrawal[1] = {
        make_withdrawal("home_welcome", "en-US", 32),
    };
    assert(
        ui_text_sync_core_plan(
            NULL,
            0,
            other_language_withdrawal,
            1,
            "zh-CN",
            "home_welcome",
            &selected
        ) == UI_TEXT_SYNC_PLAN_NONE
    );

    // An empty device language selects nothing, even with candidates present.
    assert(
        ui_text_sync_core_plan(
            single,
            1,
            NULL,
            0,
            "",
            "home_welcome",
            &selected
        ) == UI_TEXT_SYNC_PLAN_NONE
    );

    return 0;
}
