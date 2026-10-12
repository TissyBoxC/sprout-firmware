// Host test for the production security-posture decision.

#include "security_posture.h"

#include <cassert>

namespace {

security_posture_features_t features(bool secure_boot, bool flash_encrypt) {
    const security_posture_features_t snapshot = {
        .secure_boot_enabled = secure_boot,
        .flash_encryption_enabled = flash_encrypt,
    };
    return snapshot;
}

void test_development_builds_always_run(void) {
    // Bench and factory images must boot so provisioning can happen before the
    // eFuses are burned, so both controls may stay off.
    const security_posture_features_t off = features(false, false);
    assert(
        security_posture_evaluate(false, &off) ==
        SECURITY_POSTURE_VERDICT_OK
    );
    assert(
        security_posture_evaluate(false, nullptr) ==
        SECURITY_POSTURE_VERDICT_OK
    );
    const security_posture_features_t on = features(true, true);
    assert(
        security_posture_evaluate(false, &on) ==
        SECURITY_POSTURE_VERDICT_OK
    );
}

void test_production_requires_secure_boot(void) {
    const security_posture_features_t no_boot = features(false, true);
    assert(
        security_posture_evaluate(true, &no_boot) ==
        SECURITY_POSTURE_VERDICT_INSECURE_BOOT
    );
    const security_posture_features_t neither = features(false, false);
    assert(
        security_posture_evaluate(true, &neither) ==
        SECURITY_POSTURE_VERDICT_INSECURE_BOOT
    );
}

void test_production_requires_flash_encryption(void) {
    const security_posture_features_t no_encrypt = features(true, false);
    assert(
        security_posture_evaluate(true, &no_encrypt) ==
        SECURITY_POSTURE_VERDICT_UNENCRYPTED_FLASH
    );
}

void test_missing_snapshot_fails_safe(void) {
    // A production build must never treat an unreadable snapshot as hardened.
    assert(
        security_posture_evaluate(true, nullptr) ==
        SECURITY_POSTURE_VERDICT_INSECURE_BOOT
    );
}

void test_hardened_production_passes(void) {
    const security_posture_features_t hardened = features(true, true);
    assert(
        security_posture_evaluate(true, &hardened) ==
        SECURITY_POSTURE_VERDICT_OK
    );
}

}  // namespace

int main() {
    test_development_builds_always_run();
    test_production_requires_secure_boot();
    test_production_requires_flash_encryption();
    test_missing_snapshot_fails_safe();
    test_hardened_production_passes();
    return 0;
}
