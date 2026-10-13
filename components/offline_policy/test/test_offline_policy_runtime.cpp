#include "offline_policy.h"

#include <cassert>
#include <cstring>

int main() {
    assert(offline_policy_init() == ESP_OK);
    assert(offline_policy_is_ready());

    // A fresh store falls back to the conservative default before a publish.
    const offline_policy_t initial = offline_policy_get();
    assert(initial.max_cached_packages == OFFLINE_POLICY_DEFAULT_CACHED_PACKAGES);
    assert(initial.max_cache_bytes ==
           (int64_t)OFFLINE_POLICY_DEFAULT_CACHE_BYTES);
    assert(initial.revision == 0u);

    // Applying a new revision persists it and clamps out-of-range fields.
    offline_policy_t published = offline_policy_default();
    published.revision = 7u;
    published.offline_enabled = false;
    published.download_over_wifi_only = false;
    published.max_cached_packages = 40u;
    published.max_cache_bytes = 1024ull * 1024ull * 1024ull;
    published.cache_ttl_hours = 240u;
    assert(offline_policy_set(&published) == ESP_OK);

    const offline_policy_t applied = offline_policy_get();
    assert(applied.revision == 7u);
    assert(!applied.offline_enabled);
    assert(!applied.download_over_wifi_only);
    assert(applied.max_cached_packages == 40u);
    assert(applied.cache_ttl_hours == 240u);

    // An older revision is ignored so the cache budget never rolls back.
    offline_policy_t stale = published;
    stale.revision = 6u;
    stale.max_cached_packages = 5u;
    assert(offline_policy_set(&stale) == ESP_OK);
    assert(offline_policy_get().revision == 7u);
    assert(offline_policy_get().max_cached_packages == 40u);

    // A null policy is rejected.
    assert(offline_policy_set(nullptr) == ESP_ERR_INVALID_ARG);

    // The blob survives a re-init, standing in for a device restart.
    assert(offline_policy_init() == ESP_OK);
    const offline_policy_t restored = offline_policy_get();
    assert(restored.revision == 7u);
    assert(restored.max_cached_packages == 40u);
    assert(restored.cache_ttl_hours == 240u);

    assert(
        std::strcmp(
            offline_policy_error_name(OFFLINE_POLICY_ERR_INVALID_POLICY),
            "invalid_policy"
        ) == 0
    );

    return 0;
}
