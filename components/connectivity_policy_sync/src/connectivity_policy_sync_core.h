#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable connectivity-policy schema version shared with the platform. */
#define CONNECTIVITY_POLICY_SCHEMA_VERSION 1u

/** @brief The preferred radio the device should attach to when available. */
typedef enum {
    CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST = 0,
    CONNECTIVITY_POLICY_PREFERENCE_CELLULAR_FIRST,
    CONNECTIVITY_POLICY_PREFERENCE_CELLULAR_ONLY,
} connectivity_policy_preference_t;

/**
 * @brief The bounded connectivity policy the device has applied.
 *
 * This is the Wi-Fi-path mirror of the cellular policy: it keeps the applied
 * revision and the guardian's coarse preferences so `connectivity_report` can
 * prove which revision the device runs on a build that never links the modem
 * stack.
 */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t roaming_allowed;
    uint8_t remote_wake_enabled;
    uint8_t metered_upload_limit;
    uint16_t preference;
    uint32_t revision;
} connectivity_policy_state_t;

/* Pure helpers, free of ESP-IDF dependencies so the mapping is covered by host
 * tests. */

/** @brief Return the conservative default connectivity policy. */
connectivity_policy_state_t connectivity_policy_state_default(void);

/** @brief Return true when a preference string maps to a known value. */
bool connectivity_policy_preference_from_name(
    const char *name,
    connectivity_policy_preference_t *preference
);

/**
 * @brief Build one applied policy state from the platform response.
 *
 * Unknown preferences fall back to Wi-Fi-first and a negative revision is
 * treated as zero, so a malformed or partial response can never install an
 * out-of-range state.
 */
connectivity_policy_state_t connectivity_policy_sync_core_build(
    bool enabled,
    bool roaming_allowed,
    bool remote_wake_enabled,
    bool metered_upload_limit,
    const char *preference,
    int revision
);

#ifdef __cplusplus
}
#endif
