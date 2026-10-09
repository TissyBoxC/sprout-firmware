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
        ota_rollback_boot_action(false, false, 3, 3) ==
            OTA_ROLLBACK_ACTION_NONE,
        "non-pending image must not roll back"
    );
    check(
        ota_rollback_boot_action(true, true, 1, 3) ==
            OTA_ROLLBACK_ACTION_MARK_VALID,
        "healthy pending image must be marked valid"
    );
    check(
        ota_rollback_boot_action(true, false, 1, 3) ==
            OTA_ROLLBACK_ACTION_NONE,
        "unhealthy pending image must wait for budget"
    );
    check(
        ota_rollback_boot_action(true, false, 3, 3) ==
            OTA_ROLLBACK_ACTION_REBOOT_INVALID,
        "unhealthy pending image past budget must roll back"
    );
}

}  // namespace

int main() {
    test_upgrade_allowed_downgrade_requires_policy();
    test_boot_budget();
    test_boot_actions();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota rollback policy tests passed");
    return 0;
}
