#include "ui_text_store.h"

#include <cassert>
#include <cstring>

static ui_text_entry_t make_entry(
    const char *ui_text_key,
    const char *value,
    uint16_t max_length
) {
    ui_text_entry_t entry = {};
    std::strncpy(entry.ui_text_key, ui_text_key, sizeof(entry.ui_text_key) - 1);
    std::strncpy(entry.value, value, sizeof(entry.value) - 1);
    entry.max_length = max_length;
    return entry;
}

static ui_text_package_t make_package(const char *package_id, uint32_t version) {
    ui_text_package_t package = {};
    std::strncpy(package.package_id, package_id, sizeof(package.package_id) - 1);
    std::strncpy(package.language, "zh-CN", sizeof(package.language) - 1);
    package.package_version = version;
    package.published_at = 100;
    return package;
}

int main() {
    ui_text_entry_t storage[4] = {};
    ui_text_store_t store = {};
    assert(ui_text_store_init(&store, storage, 4) == UI_TEXT_STORE_OK);

    ui_text_entry_t first_entry = make_entry("home.title", "欢迎回家", 40);
    first_entry.variable_count = 1;
    std::strncpy(
        first_entry.variables[0],
        "name",
        sizeof(first_entry.variables[0]) - 1
    );
    bool changed = false;
    ui_text_package_t package = make_package("home_welcome", 1);
    assert(
        ui_text_store_apply_package(
            &store,
            &package,
            &first_entry,
            1,
            &changed
        ) == UI_TEXT_STORE_OK
    );
    assert(changed);
    assert(store.active);
    assert(store.count == 1);
    assert(std::strcmp(store.package.package_id, "home_welcome") == 0);

    size_t position = 0;
    assert(
        ui_text_store_find(&store, "home.title", &position) == UI_TEXT_STORE_OK
    );
    assert(position == 0);
    assert(std::strcmp(store.entries[0].value, "欢迎回家") == 0);
    assert(
        ui_text_store_find(&store, "missing.key", &position) ==
        UI_TEXT_STORE_ERR_NOT_FOUND
    );

    // A package without a positive version is rejected outright.
    ui_text_package_t older = make_package("home_welcome", 1);
    older.package_version = 0;
    assert(
        ui_text_store_apply_package(
            &store,
            &older,
            &first_entry,
            1,
            &changed
        ) == UI_TEXT_STORE_ERR_INVALID_ARGUMENT
    );

    // Equal version refreshes entries.
    ui_text_entry_t second_entry = make_entry("home.subtitle", "今天也要开心", 40);
    ui_text_entry_t refreshed[2] = {first_entry, second_entry};
    assert(
        ui_text_store_apply_package(
            &store,
            &package,
            refreshed,
            2,
            &changed
        ) == UI_TEXT_STORE_OK
    );
    assert(changed);
    assert(store.count == 2);

    // A newer version replaces the whole package.
    ui_text_package_t newer = make_package("home_welcome", 2);
    assert(
        ui_text_store_apply_package(
            &store,
            &newer,
            &second_entry,
            1,
            &changed
        ) == UI_TEXT_STORE_OK
    );
    assert(changed);
    assert(store.package.package_version == 2);
    assert(store.count == 1);

    // An older version than the active one is ignored without marking change.
    ui_text_package_t rollback = make_package("home_welcome", 1);
    assert(
        ui_text_store_apply_package(
            &store,
            &rollback,
            &first_entry,
            1,
            &changed
        ) == UI_TEXT_STORE_OK
    );
    assert(!changed);
    assert(store.package.package_version == 2);

    // An entry that exceeds its declared maximum length rejects the package.
    ui_text_entry_t oversized = make_entry("home.title", "这个文案太长了", 2);
    assert(
        ui_text_store_apply_package(
            &store,
            &newer,
            &oversized,
            1,
            &changed
        ) == UI_TEXT_STORE_ERR_INVALID_ARGUMENT
    );
    assert(store.count == 1);

    // A package with more entries than capacity is rejected as full.
    ui_text_entry_t overflow[5] = {
        make_entry("home.a", "a", 4),
        make_entry("home.b", "b", 4),
        make_entry("home.c", "c", 4),
        make_entry("home.d", "d", 4),
        make_entry("home.e", "e", 4),
    };
    assert(
        ui_text_store_apply_package(
            &store,
            &newer,
            overflow,
            5,
            &changed
        ) == UI_TEXT_STORE_ERR_FULL
    );

    // Withdrawing a different id is a no-op; the matching id clears the store.
    ui_text_package_t other = make_package("home_other", 3);
    assert(
        ui_text_store_apply_package(
            &store,
            &other,
            &first_entry,
            1,
            &changed
        ) == UI_TEXT_STORE_OK
    );
    assert(store.active);
    assert(
        ui_text_store_withdraw(&store, "home_other", &changed) ==
        UI_TEXT_STORE_OK
    );
    assert(changed);
    assert(!store.active);
    assert(store.count == 0);
    assert(
        ui_text_store_find(&store, "home.title", &position) ==
        UI_TEXT_STORE_ERR_NOT_FOUND
    );

    return 0;
}
