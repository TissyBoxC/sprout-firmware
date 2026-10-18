#include "ota_rollback_policy.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_upgrade_allowed_downgrade_requires_policy() {
    check(
        ota_rollback_version_allowed("0.9.0", "0.10.0", false, false),
        "upgrade must be allowed"
    );
    check(
        !ota_rollback_version_allowed("0.10.0", "0.9.0", false, true),
        "downgrade must require rollback_allowed"
    );
    check(
        ota_rollback_version_allowed("0.10.0", "0.9.0", true, true),
        "downgrade with rollback policy must be allowed"
    );
    check(
        !ota_rollback_version_allowed("0.10.0", "0.9.0", true, false),
        "downgrade must still require an invalid current image"
    );
}

void test_boot_budget() {
    check(!ota_rollback_boot_count_exceeded(0, 3), "boot 0 under budget");
    check(!ota_rollback_boot_count_exceeded(2, 3), "boot 2 under budget");
    check(ota_rollback_boot_count_exceeded(3, 3), "boot 3 exhausts budget");
    check(!ota_rollback_boot_count_exceeded(3, 0), "zero budget disables rollback");
}

void test_boot_actions() {
    check(
        ota_rollback_health_action(false, false, 3, 3) ==
            OTA_ROLLBACK_ACTION_NONE,
        "non-pending image must not consume health failures"
    );
    check(
        ota_rollback_health_action(true, true, 2, 3) ==
            OTA_ROLLBACK_ACTION_MARK_VALID,
        "successful probe must mark a pending image valid"
    );
    check(
        ota_rollback_health_action(true, false, 1, 3) ==
            OTA_ROLLBACK_ACTION_NONE,
        "one failed authenticated probe must keep the image pending"
    );
    check(
        ota_rollback_health_action(true, false, 3, 3) ==
            OTA_ROLLBACK_ACTION_REBOOT_INVALID,
        "failed authenticated probes at the budget must roll back"
    );
}

void test_health_probe_failure_classification() {
    check(
        !ota_rollback_health_probe_failure_is_definitive(false, 0, false),
        "transport failure must not consume the rollback budget"
    );
    check(
        !ota_rollback_health_probe_failure_is_definitive(true, 0, false),
        "missing HTTP response must not consume the rollback budget"
    );
    check(
        ota_rollback_health_probe_failure_is_definitive(true, 404, false),
        "definitive HTTP 404 must consume the rollback budget"
    );
    check(
        ota_rollback_health_probe_failure_is_definitive(true, 401, false),
        "rejected device session must consume the rollback budget"
    );
    check(
        !ota_rollback_health_probe_failure_is_definitive(true, 503, false),
        "transient backend unavailability must not consume the budget"
    );
    check(
        !ota_rollback_health_probe_failure_is_definitive(true, 429, false),
        "rate limiting must not consume the budget"
    );
    check(
        ota_rollback_health_probe_failure_is_definitive(true, 200, true),
        "a malformed successful response must consume the budget"
    );
}

}  // namespace

int main() {
    test_upgrade_allowed_downgrade_requires_policy();
    test_boot_budget();
    test_boot_actions();
    test_health_probe_failure_classification();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota rollback policy tests passed");
    return 0;
}
