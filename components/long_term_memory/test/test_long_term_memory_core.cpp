#include "long_term_memory_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static long_term_memory_entry_t make_entry(
    const char *entry_id,
    const char *child_id,
    const char *key,
    const char *value
) {
    long_term_memory_entry_t entry = {};
    std::strncpy(entry.entry_id, entry_id, LONG_TERM_MEMORY_ID_SIZE - 1);
    std::strncpy(
        entry.child_id,
        child_id,
        LONG_TERM_MEMORY_CHILD_ID_SIZE - 1
    );
    std::strncpy(entry.key, key, LONG_TERM_MEMORY_KEY_SIZE - 1);
    std::strncpy(entry.value, value, LONG_TERM_MEMORY_VALUE_SIZE - 1);
    entry.updated_at_epoch_seconds = 1700000000;
    return entry;
}

int main() {
    long_term_memory_core_t core = {};
    long_term_memory_core_init(&core);
    assert(core.count == 0);

    const long_term_memory_entry_t favorite = make_entry(
        "entry_001",
        "child_001",
        "favorite_color",
        "蓝色"
    );
    assert(
        long_term_memory_core_remember(&core, &favorite) ==
        LONG_TERM_MEMORY_OK
    );
    assert(core.count == 1);

    const long_term_memory_entry_t pet = make_entry(
        "entry_002",
        "child_001",
        "pet_name",
        "布丁"
    );
    assert(long_term_memory_core_remember(&core, &pet) == LONG_TERM_MEMORY_OK);
    assert(core.count == 2);

    // Re-learning the same child+key replaces the value in place.
    long_term_memory_entry_t favorite_v2 = make_entry(
        "entry_001b",
        "child_001",
        "favorite_color",
        "绿色"
    );
    assert(
        long_term_memory_core_remember(&core, &favorite_v2) ==
        LONG_TERM_MEMORY_OK
    );
    assert(core.count == 2);
    assert(
        long_term_memory_core_find(&core, "child_001", "favorite_color") == 0
    );
    assert(std::strcmp(core.entries[0].value, "绿色") == 0);

    // Memory is scoped per child: the same key for another child is distinct.
    const long_term_memory_entry_t sibling = make_entry(
        "entry_003",
        "child_002",
        "favorite_color",
        "红色"
    );
    assert(
        long_term_memory_core_remember(&core, &sibling) == LONG_TERM_MEMORY_OK
    );
    assert(core.count == 3);
    assert(
        long_term_memory_core_find(&core, "child_002", "favorite_color") == 2
    );
    assert(
        long_term_memory_core_find(&core, "child_003", "favorite_color") ==
        core.count
    );

    // Invalid entries are rejected.
    long_term_memory_entry_t invalid = make_entry("", "child_001", "k", "v");
    assert(
        long_term_memory_core_remember(&core, &invalid) ==
        LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT
    );
    invalid = make_entry("entry_004", "child_001", "", "v");
    assert(
        long_term_memory_core_remember(&core, &invalid) ==
        LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT
    );
    invalid = make_entry("entry_004", "child_001", "k", "");
    assert(
        long_term_memory_core_remember(&core, &invalid) ==
        LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT
    );

    // Forgetting a missing entry is success; forgetting one drops one row.
    assert(
        long_term_memory_core_forget(&core, "entry_missing") ==
        LONG_TERM_MEMORY_OK
    );
    assert(core.count == 3);
    assert(
        long_term_memory_core_forget(&core, "entry_002") == LONG_TERM_MEMORY_OK
    );
    assert(core.count == 2);

    // Forgetting a child drops every entry for that child.
    size_t removed = 0;
    assert(
        long_term_memory_core_forget_child(&core, "child_001", &removed) ==
        LONG_TERM_MEMORY_OK
    );
    assert(removed == 1);
    assert(core.count == 1);

    // Clearing wipes the store (used when authorization is revoked).
    long_term_memory_core_clear(&core);
    assert(core.count == 0);

    // Capacity is bounded: inserting past the limit reports FULL.
    long_term_memory_core_t full = {};
    long_term_memory_core_init(&full);
    char id[LONG_TERM_MEMORY_ID_SIZE];
    char key[LONG_TERM_MEMORY_KEY_SIZE];
    for (int index = 0; index < LONG_TERM_MEMORY_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "entry_%03d", index);
        std::snprintf(key, sizeof(key), "key_%03d", index);
        const long_term_memory_entry_t entry = make_entry(
            id,
            "child_001",
            key,
            "值"
        );
        assert(
            long_term_memory_core_remember(&full, &entry) ==
            LONG_TERM_MEMORY_OK
        );
    }
    assert(full.count == LONG_TERM_MEMORY_CAPACITY);
    const long_term_memory_entry_t overflow = make_entry(
        "entry_overflow",
        "child_001",
        "key_overflow",
        "溢出"
    );
    assert(
        long_term_memory_core_remember(&full, &overflow) ==
        LONG_TERM_MEMORY_ERR_FULL
    );
    assert(
        long_term_memory_core_forget(&full, "entry_000") ==
        LONG_TERM_MEMORY_OK
    );
    assert(
        long_term_memory_core_remember(&full, &overflow) == LONG_TERM_MEMORY_OK
    );
    assert(full.count == LONG_TERM_MEMORY_CAPACITY);

    return 0;
}
