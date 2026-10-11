#include "growth_record_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static growth_record_t make_record(
    const char *record_id,
    const char *child_id,
    growth_category_t category,
    const char *title
) {
    growth_record_t record = {};
    std::strncpy(record.record_id, record_id, GROWTH_RECORD_ID_SIZE - 1);
    std::strncpy(record.child_id, child_id, GROWTH_RECORD_CHILD_ID_SIZE - 1);
    record.category = category;
    std::strncpy(record.title, title, GROWTH_RECORD_TITLE_SIZE - 1);
    record.recorded_at_epoch_seconds = 1700000000;
    return record;
}

int main() {
    growth_record_core_t core = {};
    growth_record_core_init(&core);
    assert(core.count == 0);

    const growth_record_t milestone = make_record(
        "record_001",
        "child_001",
        GROWTH_CATEGORY_MILESTONE,
        "第一次自己穿鞋"
    );
    assert(growth_record_core_upsert(&core, &milestone) == GROWTH_RECORD_OK);
    assert(core.count == 1);
    assert(std::strcmp(core.records[0].record_id, "record_001") == 0);

    const growth_record_t encouragement = make_record(
        "record_002",
        "child_001",
        GROWTH_CATEGORY_ENCOURAGEMENT,
        "主动整理玩具"
    );
    assert(
        growth_record_core_upsert(&core, &encouragement) == GROWTH_RECORD_OK
    );
    assert(core.count == 2);
    // Newest change leads.
    assert(std::strcmp(core.records[0].record_id, "record_002") == 0);

    // Re-upserting an existing record replaces it and moves it to the head.
    growth_record_t milestone_v2 = make_record(
        "record_001",
        "child_001",
        GROWTH_CATEGORY_MILESTONE,
        "第一次自己穿鞋（已确认）"
    );
    assert(growth_record_core_upsert(&core, &milestone_v2) == GROWTH_RECORD_OK);
    assert(core.count == 2);
    assert(std::strcmp(core.records[0].record_id, "record_001") == 0);

    // Find reports the index, or the count when absent.
    assert(growth_record_core_find(&core, "record_001") == 0);
    assert(growth_record_core_find(&core, "record_002") == 1);
    assert(growth_record_core_find(&core, "record_missing") == core.count);

    // Invalid records are rejected.
    growth_record_t invalid = make_record(
        "",
        "child_001",
        GROWTH_CATEGORY_MILESTONE,
        "空编号"
    );
    assert(
        growth_record_core_upsert(&core, &invalid) ==
        GROWTH_RECORD_ERR_INVALID_ARGUMENT
    );
    invalid = make_record("record_003", "child_001", GROWTH_CATEGORY_MILESTONE, "");
    assert(
        growth_record_core_upsert(&core, &invalid) ==
        GROWTH_RECORD_ERR_INVALID_ARGUMENT
    );
    invalid = make_record("record_003", "child_001", GROWTH_CATEGORY_MILESTONE, "标题");
    invalid.category = static_cast<growth_category_t>(99);
    assert(
        growth_record_core_upsert(&core, &invalid) ==
        GROWTH_RECORD_ERR_INVALID_ARGUMENT
    );

    // Removing a missing record is success.
    assert(
        growth_record_core_remove(&core, "record_missing") == GROWTH_RECORD_OK
    );
    assert(core.count == 2);

    // Removing one record for a child drops exactly one entry.
    size_t removed = 0;
    assert(
        growth_record_core_remove_child(&core, "child_001", &removed) ==
        GROWTH_RECORD_OK
    );
    assert(removed == 2);
    assert(core.count == 0);

    // Capacity is bounded: inserting past the limit reports FULL.
    growth_record_core_t full = {};
    growth_record_core_init(&full);
    char id[GROWTH_RECORD_ID_SIZE];
    for (int index = 0; index < GROWTH_RECORD_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "record_%03d", index);
        const growth_record_t record = make_record(
            id,
            "child_001",
            GROWTH_CATEGORY_HABIT,
            "习惯"
        );
        assert(growth_record_core_upsert(&full, &record) == GROWTH_RECORD_OK);
    }
    assert(full.count == GROWTH_RECORD_CAPACITY);
    const growth_record_t overflow = make_record(
        "record_overflow",
        "child_001",
        GROWTH_CATEGORY_HABIT,
        "溢出"
    );
    assert(
        growth_record_core_upsert(&full, &overflow) == GROWTH_RECORD_ERR_FULL
    );
    // Removing one frees exactly one slot.
    assert(growth_record_core_remove(&full, "record_000") == GROWTH_RECORD_OK);
    assert(growth_record_core_upsert(&full, &overflow) == GROWTH_RECORD_OK);
    assert(full.count == GROWTH_RECORD_CAPACITY);

    return 0;
}
