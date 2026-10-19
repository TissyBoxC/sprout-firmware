#include "ota_manager_deployment_contract.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

ota_manager_deployment_context_t normal_context() {
    ota_manager_deployment_context_t context = {};
    std::strcpy(context.status, "queued");
    std::strcpy(context.current_version, "0.12.0");
    std::strcpy(context.target_version, "0.13.0");
    std::strcpy(context.deployment_id, "deployment_install_1");
    context.kind = OTA_MANAGER_DEPLOYMENT_KIND_INSTALL;
    context.update_available = true;
    return context;
}

ota_manager_deployment_context_t rollback_context() {
    ota_manager_deployment_context_t context = {};
    std::strcpy(context.status, "queued");
    std::strcpy(context.current_version, "0.13.0");
    std::strcpy(context.target_version, "0.12.0");
    std::strcpy(context.deployment_id, "deployment_rollback_1");
    std::strcpy(
        context.rollback_release_id,
        "release_known_good_0_12_0"
    );
    std::strcpy(
        context.rollback_of_deployment_id,
        "deployment_install_failed_1"
    );
    context.kind = OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK;
    context.update_available = true;
    context.rollback_available = true;
    return context;
}

void test_normal_upgrade_requires_matching_target() {
    const ota_manager_deployment_context_t context = normal_context();
    check(
        ota_manager_validate_deployment_context(
            &context,
            "release_0_13_0",
            "0.13.0",
            false
        ) == OTA_MANAGER_DEPLOYMENT_OK,
        "normal upgrade must validate"
    );
    ota_manager_deployment_context_t mismatch = context;
    std::strcpy(mismatch.target_version, "0.13.1");
    check(
        ota_manager_validate_deployment_context(
            &mismatch,
            "release_0_13_0",
            "0.13.0",
            false
        ) == OTA_MANAGER_DEPLOYMENT_CONTRADICTORY,
        "normal upgrade target must match the signed release"
    );
    ota_manager_deployment_context_t unavailable = context;
    unavailable.update_available = false;
    check(
        ota_manager_validate_deployment_context(
            &unavailable,
            "release_0_13_0",
            "0.13.0",
            false
        ) == OTA_MANAGER_DEPLOYMENT_CONTRADICTORY,
        "enqueued update with no available target must fail closed"
    );
}

void test_deployment_kind_is_explicit_and_strict() {
    check(
        ota_manager_deployment_kind_from_protocol("install") ==
            OTA_MANAGER_DEPLOYMENT_KIND_INSTALL,
        "install must be parsed explicitly"
    );
    check(
        ota_manager_deployment_kind_from_protocol("rollback") ==
            OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK,
        "rollback must be parsed explicitly"
    );
    check(
        ota_manager_deployment_kind_from_protocol("") ==
            OTA_MANAGER_DEPLOYMENT_KIND_UNKNOWN,
        "missing deployment kind must remain unknown"
    );
    check(
        ota_manager_deployment_kind_from_protocol("Install") ==
            OTA_MANAGER_DEPLOYMENT_KIND_UNKNOWN,
        "deployment kind must be case-sensitive"
    );
}

void test_authorized_rollback_requires_explicit_contract() {
    const ota_manager_deployment_context_t context = rollback_context();
    check(
        ota_manager_validate_deployment_context(
            &context,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_OK,
        "authorized rollback must validate"
    );
    check(
        ota_manager_validate_deployment_context(
            &context,
            "release_known_good_0_12_0",
            "0.12.0",
            false
        ) == OTA_MANAGER_DEPLOYMENT_ROLLBACK_NOT_ALLOWED,
        "signed rollback permission is mandatory"
    );
}

void test_rollback_missing_or_contradictory_fields_fail() {
    const ota_manager_deployment_context_t context = rollback_context();
    ota_manager_deployment_context_t missing_deployment = context;
    missing_deployment.deployment_id[0] = '\0';
    check(
        ota_manager_validate_deployment_context(
            &missing_deployment,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_MISSING_FIELD,
        "rollback deployment id is mandatory"
    );

    ota_manager_deployment_context_t missing_previous = context;
    missing_previous.rollback_of_deployment_id[0] = '\0';
    check(
        ota_manager_validate_deployment_context(
            &missing_previous,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_MISSING_FIELD,
        "rollback source deployment id is mandatory"
    );

    ota_manager_deployment_context_t wrong_release = context;
    std::strcpy(wrong_release.rollback_release_id, "release_other");
    check(
        ota_manager_validate_deployment_context(
            &wrong_release,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_CONTRADICTORY,
        "rollback release id must select the nested release"
    );

    ota_manager_deployment_context_t wrong_target = context;
    std::strcpy(wrong_target.target_version, "0.11.0");
    check(
        ota_manager_validate_deployment_context(
            &wrong_target,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_CONTRADICTORY,
        "rollback target version must match the nested release"
    );

    ota_manager_deployment_context_t unavailable = context;
    unavailable.rollback_available = false;
    check(
        ota_manager_validate_deployment_context(
            &unavailable,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_MISSING_FIELD,
        "rollback unavailable must fail closed"
    );

    ota_manager_deployment_context_t unsafe_id = context;
    std::strcpy(unsafe_id.deployment_id, "deployment rollback 1");
    check(
        ota_manager_validate_deployment_context(
            &unsafe_id,
            "release_known_good_0_12_0",
            "0.12.0",
            true
        ) == OTA_MANAGER_DEPLOYMENT_MISSING_FIELD,
        "deployment ids must use the platform identifier alphabet"
    );

    ota_manager_deployment_context_t install_with_rollback = normal_context();
    std::strcpy(
        install_with_rollback.rollback_of_deployment_id,
        "deployment_other"
    );
    check(
        ota_manager_validate_deployment_context(
            &install_with_rollback,
            "release_0_13_0",
            "0.13.0",
            false
        ) == OTA_MANAGER_DEPLOYMENT_CONTRADICTORY,
        "normal upgrade must not carry rollback-only fields"
    );
}

void test_idempotency_includes_deployment_and_release() {
    char identity[160] = {};
    ota_manager_format_idempotency_key(
        "deployment_1",
        "release_1",
        identity,
        sizeof(identity)
    );
    check(
        std::strcmp(identity, "deployment_1|release_1") == 0,
        "identity must include deployment and release ids"
    );
    check(
        ota_manager_transaction_identity_matches(
            "deployment_1",
            "release_1",
            "deployment_1",
            "release_1"
        ),
        "full identity must match"
    );
    check(
        !ota_manager_transaction_identity_matches(
            "deployment_1",
            "release_1",
            "deployment_1",
            "release_2"
        ),
        "same deployment with another release must not replay"
    );
    check(
        !ota_manager_transaction_identity_matches(
            "deployment_2",
            "release_1",
            "deployment_1",
            "release_1"
        ),
        "same release under a new deployment must execute"
    );
    char too_small[8] = {};
    ota_manager_format_idempotency_key(
        "deployment_1",
        "release_1",
        too_small,
        sizeof(too_small)
    );
    check(
        too_small[0] == '\0',
        "identity formatting must fail closed on small buffers"
    );
    char destination[16] = {};
    check(
        !ota_manager_copy_exact_platform_field(
            "deployment_identifier_too_long",
            destination,
            sizeof(destination)
        ) && destination[0] == '\0',
        "oversized platform ids must be rejected, never truncated"
    );
    check(
        ota_manager_copy_exact_platform_field(
            "deployment_1",
            destination,
            sizeof(destination)
        ) && std::strcmp(destination, "deployment_1") == 0,
        "in-range platform ids must copy exactly"
    );
}

void test_replacement_respects_unfinished_transaction() {
    const ota_manager_deployment_context_t candidate = rollback_context();
    check(
        ota_manager_deployment_can_replace(
            &candidate,
            "release_known_good_0_12_0",
            "different_deployment",
            "release_known_good_0_12_0",
            false
        ),
        "completed state may be replaced by a newer deployment"
    );
    check(
        !ota_manager_deployment_can_replace(
            &candidate,
            "release_known_good_0_12_0",
            "different_deployment",
            "release_known_good_0_12_0",
            true
        ),
        "unfinished state must reject a different deployment"
    );
    check(
        ota_manager_deployment_can_replace(
            &candidate,
            "release_known_good_0_12_0",
            "deployment_rollback_1",
            "release_known_good_0_12_0",
            true
        ),
        "exact replay may proceed to idempotent recovery"
    );
}

void test_transaction_kind_round_trip_is_strict() {
    check(
        ota_manager_transaction_kind_is_valid(
            OTA_MANAGER_DEPLOYMENT_KIND_INSTALL
        ),
        "install transaction kind must be valid"
    );
    check(
        ota_manager_transaction_kind_is_valid(
            OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK
        ),
        "rollback transaction kind must be valid"
    );
    check(
        !ota_manager_transaction_kind_is_valid(
            OTA_MANAGER_DEPLOYMENT_KIND_UNKNOWN
        ),
        "unknown transaction kind must be invalid"
    );
}

}  // namespace

int main() {
    test_normal_upgrade_requires_matching_target();
    test_deployment_kind_is_explicit_and_strict();
    test_authorized_rollback_requires_explicit_contract();
    test_rollback_missing_or_contradictory_fields_fail();
    test_idempotency_includes_deployment_and_release();
    test_replacement_respects_unfinished_transaction();
    test_transaction_kind_round_trip_is_strict();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota manager deployment contract tests passed");
    return 0;
}
