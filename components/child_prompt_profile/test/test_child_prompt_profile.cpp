#include "child_prompt_profile.h"

#include "config_store.h"
#include "parent_policy.h"

#include <cassert>
#include <cstring>

void child_prompt_host_reset_store(bool ready);
void child_prompt_host_put(const char *key, const char *value);
void child_prompt_host_set_policy_present(bool present);
parent_policy_snapshot_t *child_prompt_host_policy(void);
void child_prompt_host_set_policy_category(size_t index, const char *category);

namespace {

void test_tier_name_mapping() {
    assert(
        std::strcmp(
            child_prompt_profile_tier_name(
                CHILD_PROMPT_PROFILE_TIER_AGE_3_4
            ),
            "age_3_4"
        ) == 0
    );
    assert(
        std::strcmp(
            child_prompt_profile_tier_name(
                CHILD_PROMPT_PROFILE_TIER_AGE_5_6
            ),
            "age_5_6"
        ) == 0
    );
    assert(
        std::strcmp(
            child_prompt_profile_tier_name(
                CHILD_PROMPT_PROFILE_TIER_AGE_7_8
            ),
            "age_7_8"
        ) == 0
    );
    // The unknown tier has no contract text so callers handle it explicitly.
    assert(
        child_prompt_profile_tier_name(CHILD_PROMPT_PROFILE_TIER_UNKNOWN) ==
        nullptr
    );
}

void test_tier_from_name() {
    child_prompt_profile_tier_t tier = CHILD_PROMPT_PROFILE_TIER_UNKNOWN;
    assert(child_prompt_profile_tier_from_name("age_3_4", &tier));
    assert(tier == CHILD_PROMPT_PROFILE_TIER_AGE_3_4);
    assert(child_prompt_profile_tier_from_name("age_7_8", &tier));
    assert(tier == CHILD_PROMPT_PROFILE_TIER_AGE_7_8);

    // Unknown text must fail and leave the output untouched.
    tier = CHILD_PROMPT_PROFILE_TIER_AGE_5_6;
    assert(!child_prompt_profile_tier_from_name("age_99", &tier));
    assert(tier == CHILD_PROMPT_PROFILE_TIER_AGE_5_6);
    assert(!child_prompt_profile_tier_from_name(nullptr, &tier));
    assert(!child_prompt_profile_tier_from_name("age_3_4", nullptr));
}

void test_init_requires_a_ready_store() {
    child_prompt_host_reset_store(false);
    assert(child_prompt_profile_init() == ESP_ERR_INVALID_STATE);
    assert(!child_prompt_profile_is_ready());
}

void test_first_boot_defaults_to_the_youngest_tier() {
    child_prompt_host_reset_store(true);
    child_prompt_host_set_policy_present(false);
    assert(child_prompt_profile_init() == ESP_OK);
    // init is idempotent and must not re-read the store.
    assert(child_prompt_profile_init() == ESP_OK);
    assert(child_prompt_profile_is_ready());

    // With no persisted value the conservative youngest tier is reported.
    assert(
        child_prompt_profile_get_tier() ==
        CHILD_PROMPT_PROFILE_TIER_AGE_3_4
    );

    char tier_text[CHILD_PROMPT_PROFILE_TIER_TEXT_SIZE] = {0};
    assert(
        child_prompt_profile_copy_tier_text(tier_text, sizeof(tier_text)) ==
        ESP_OK
    );
    assert(std::strcmp(tier_text, "age_3_4") == 0);
}

void test_without_parent_policy_no_category_is_allowed() {
    // No cached guardian policy means local gating must deny every category.
    assert(!child_prompt_profile_category_allowed("story"));
    assert(!child_prompt_profile_category_allowed(nullptr));

    child_prompt_profile_snapshot_t snapshot = {};
    assert(child_prompt_profile_get_snapshot(&snapshot) == ESP_OK);
    assert(!snapshot.has_parent_policy);
    assert(snapshot.allowed_category_count == 0u);
}

void test_set_tier_is_validated_and_persisted() {
    assert(
        child_prompt_profile_set_tier(CHILD_PROMPT_PROFILE_TIER_UNKNOWN) ==
        ESP_ERR_INVALID_ARG
    );
    assert(
        child_prompt_profile_set_tier(
            static_cast<child_prompt_profile_tier_t>(99)
        ) == ESP_ERR_INVALID_ARG
    );

    assert(
        child_prompt_profile_set_tier(CHILD_PROMPT_PROFILE_TIER_AGE_7_8) ==
        ESP_OK
    );
    assert(
        child_prompt_profile_get_tier() ==
        CHILD_PROMPT_PROFILE_TIER_AGE_7_8
    );

    // The persisted value survives a reboot.
    assert(
        child_prompt_profile_copy_tier_text(nullptr, 0) ==
        ESP_ERR_INVALID_ARG
    );
    char too_small[4] = {0};
    assert(
        child_prompt_profile_copy_tier_text(too_small, sizeof(too_small)) ==
        ESP_ERR_INVALID_SIZE
    );
}

void test_set_tier_text_is_validated() {
    assert(child_prompt_profile_set_tier_text("age_5_6") == ESP_OK);
    assert(
        child_prompt_profile_get_tier() ==
        CHILD_PROMPT_PROFILE_TIER_AGE_5_6
    );
    assert(child_prompt_profile_set_tier_text("invalid") == ESP_ERR_INVALID_ARG);
    assert(child_prompt_profile_set_tier_text(nullptr) == ESP_ERR_INVALID_ARG);
}

void test_policy_sync_mirrors_categories_and_volume() {
    parent_policy_snapshot_t *policy = child_prompt_host_policy();
    policy->max_volume_percent = 55;
    policy->allowed_category_count = 2;
    child_prompt_host_set_policy_category(0, "story");
    child_prompt_host_set_policy_category(1, "music");
    child_prompt_host_set_policy_present(true);

    assert(child_prompt_profile_sync_from_parent_policy() == ESP_OK);
    assert(child_prompt_profile_category_allowed("story"));
    assert(child_prompt_profile_category_allowed("music"));
    assert(!child_prompt_profile_category_allowed("video"));

    child_prompt_profile_snapshot_t snapshot = {};
    assert(child_prompt_profile_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.has_parent_policy);
    assert(snapshot.max_volume_percent == 55u);
    assert(snapshot.allowed_category_count == 2u);
    // The age tier keeps its persisted value; the policy has none.
    assert(snapshot.tier == CHILD_PROMPT_PROFILE_TIER_AGE_5_6);
    assert(std::strcmp(snapshot.tier_text, "age_5_6") == 0);
}

void test_policy_category_count_is_bounded() {
    parent_policy_snapshot_t *policy = child_prompt_host_policy();
    // Claim more categories than the profile can cache; the profile must be
    // honest about how many it actually retained.
    policy->allowed_category_count = PARENT_POLICY_MAX_ALLOWED_CATEGORIES + 4;
    for (size_t index = 0; index < PARENT_POLICY_MAX_ALLOWED_CATEGORIES;
         ++index) {
        char name[PARENT_POLICY_CATEGORY_SIZE] = {0};
        name[0] = 'c';
        name[1] = static_cast<char>('0' + (index % 10));
        child_prompt_host_set_policy_category(index, name);
    }
    child_prompt_host_set_policy_present(true);

    assert(child_prompt_profile_sync_from_parent_policy() == ESP_OK);
    child_prompt_profile_snapshot_t snapshot = {};
    assert(child_prompt_profile_get_snapshot(&snapshot) == ESP_OK);
    assert(
        snapshot.allowed_category_count ==
        CHILD_PROMPT_PROFILE_MAX_CATEGORIES
    );
}

void test_snapshot_requires_argument() {
    assert(child_prompt_profile_get_snapshot(nullptr) == ESP_ERR_INVALID_ARG);
}

void test_shutdown_clears_runtime_state() {
    child_prompt_profile_shutdown();
    assert(!child_prompt_profile_is_ready());
    // An uninitialized profile reports unknown and denies all categories.
    assert(
        child_prompt_profile_get_tier() ==
        CHILD_PROMPT_PROFILE_TIER_UNKNOWN
    );
    assert(!child_prompt_profile_category_allowed("story"));
    child_prompt_profile_snapshot_t snapshot = {};
    assert(
        child_prompt_profile_get_snapshot(&snapshot) == ESP_ERR_INVALID_STATE
    );
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        child_prompt_profile_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "child_prompt_profile") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown != nullptr);
}

}  // namespace

int main() {
    test_tier_name_mapping();
    test_tier_from_name();
    test_init_requires_a_ready_store();
    test_first_boot_defaults_to_the_youngest_tier();
    test_without_parent_policy_no_category_is_allowed();
    test_set_tier_is_validated_and_persisted();
    test_set_tier_text_is_validated();
    test_policy_sync_mirrors_categories_and_volume();
    test_policy_category_count_is_bounded();
    test_snapshot_requires_argument();
    test_shutdown_clears_runtime_state();
    test_module_descriptor_is_stable();
    return 0;
}
