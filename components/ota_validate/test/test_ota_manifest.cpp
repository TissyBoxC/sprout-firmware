#include "ota_manifest.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

const char *const VALID_MANIFEST =
    "{"
    "\"schema_version\":\"1\","
    "\"release_id\":\"rel-2026-10-09-001\","
    "\"firmware_version\":\"0.10.0\","
    "\"hardware_revision\":\"esp32s3\","
    "\"channel\":\"stable\","
    "\"artifact_url\":\"https://download.clarkhub.cn/firmware/sprout.bin\","
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
    "\"size_bytes\":1048576,"
    "\"signature_key_id\":\"release-key-1\","
    "\"signature_algorithm\":\"ed25519\","
    "\"signature\":\"base64-signature\","
    "\"rollback_allowed\":false,"
    "\"published_at\":\"2026-10-09T00:00:00Z\","
    "\"min_source_version\":\"0.9.0\""
    "}";

void test_valid_manifest_parses() {
    ota_manifest_t manifest = {};
    const ota_manifest_error_t result = ota_manifest_parse(
        VALID_MANIFEST,
        std::strlen(VALID_MANIFEST),
        &manifest
    );
    check(result == OTA_MANIFEST_OK, "valid manifest must parse");
    check(
        std::strcmp(manifest.release_id, "rel-2026-10-09-001") == 0,
        "release id must round-trip"
    );
    check(manifest.size_bytes == 1048576, "size must round-trip");
    check(!manifest.rollback_allowed, "rollback flag must round-trip");
}

void test_missing_required_field_is_rejected() {
    const char *json =
        "{\"schema_version\":\"1\",\"release_id\":\"r1\"}";
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(json, std::strlen(json), &manifest) ==
            OTA_MANIFEST_ERR_MISSING_FIELD,
        "missing fields must be rejected"
    );
}

void test_duplicate_field_is_rejected() {
    const char *json =
        "{"
        "\"schema_version\":\"1\","
        "\"schema_version\":\"1\","
        "\"release_id\":\"r1\""
        "}";
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(json, std::strlen(json), &manifest) ==
            OTA_MANIFEST_ERR_DUPLICATE_FIELD,
        "duplicate manifest fields must be rejected"
    );
}

void test_unsupported_schema_is_rejected() {
    char json[1024] = {};
    std::strcpy(json, VALID_MANIFEST);
    char *entry = std::strstr(json, "\"schema_version\":\"1\"");
    check(entry != nullptr, "schema field must be present");
    entry[18] = '2';
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(json, std::strlen(json), &manifest) ==
            OTA_MANIFEST_ERR_SCHEMA_VERSION,
        "unsupported schema version must be rejected"
    );
}

void test_cleartext_artifact_is_rejected() {
    char json[1024] = {};
    std::strcpy(json, VALID_MANIFEST);
    char *entry = std::strstr(json, "https://download.clarkhub.cn");
    check(entry != nullptr, "artifact URL must be present");
    std::memcpy(entry, "http://", 7);
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(json, std::strlen(json), &manifest) ==
            OTA_MANIFEST_ERR_ARTIFACT_URL,
        "cleartext artifact URL must be rejected"
    );
}

void test_sha256_validation() {
    check(
        ota_manifest_sha256_is_valid(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        ),
        "valid SHA-256 must pass"
    );
    check(
        !ota_manifest_sha256_is_valid(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde"
        ),
        "short SHA-256 must fail"
    );
    check(
        !ota_manifest_sha256_is_valid(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"
        ),
        "non-hex SHA-256 must fail"
    );
}

void test_signature_fields_fail_closed() {
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(
            VALID_MANIFEST,
            std::strlen(VALID_MANIFEST),
            &manifest
        ) == OTA_MANIFEST_OK,
        "signed fixture must parse"
    );
    check(
        std::strcmp(manifest.signature_algorithm, "ed25519") == 0,
        "signature algorithm must round-trip"
    );

    char json[1024] = {};
    std::strcpy(json, VALID_MANIFEST);
    char *algorithm = std::strstr(json, "\"signature_algorithm\":\"ed25519\"");
    check(algorithm != nullptr, "algorithm field must be present");
    algorithm[26] = 'x';
    check(
        ota_manifest_parse(json, std::strlen(json), &manifest) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "unsupported signature algorithm must fail closed"
    );

    std::strcpy(json, VALID_MANIFEST);
    char *key_id = std::strstr(json, "\"signature_key_id\":\"release-key-1\"");
    check(key_id != nullptr, "key field must be present");
    key_id[26] = ' ';
    check(
        ota_manifest_parse(json, std::strlen(json), &manifest) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "key ids with ambiguous whitespace must fail closed"
    );
}

void test_device_policy_rejects_mismatches() {
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(
            VALID_MANIFEST,
            std::strlen(VALID_MANIFEST),
            &manifest
        ) == OTA_MANIFEST_OK,
        "fixture manifest must parse"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.9.5",
            "esp32s3",
            "stable",
            false
        ) == OTA_MANIFEST_OK,
        "matching device must accept newer release"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.9.5",
            "esp32c3",
            "stable",
            false
        ) == OTA_MANIFEST_ERR_HARDWARE_REVISION,
        "hardware mismatch must be rejected"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.9.5",
            "esp32s3",
            "canary",
            false
        ) == OTA_MANIFEST_ERR_CHANNEL,
        "channel mismatch must be rejected"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.8.0",
            "esp32s3",
            "stable",
            false
        ) == OTA_MANIFEST_ERR_VERSION_TOO_OLD,
        "source version below minimum must be rejected"
    );
}

void test_anti_downgrade_and_rollback_policy() {
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(
            VALID_MANIFEST,
            std::strlen(VALID_MANIFEST),
            &manifest
        ) == OTA_MANIFEST_OK,
        "fixture manifest must parse"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.11.0",
            "esp32s3",
            "stable",
            false
        ) == OTA_MANIFEST_ERR_ROLLBACK_NOT_ALLOWED,
        "silent downgrade must be rejected"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.11.0",
            "esp32s3",
            "stable",
            true
        ) == OTA_MANIFEST_ERR_ROLLBACK_NOT_ALLOWED,
        "manifest rollback_allowed=false must still reject downgrade"
    );
    manifest.rollback_allowed = true;
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.11.0",
            "esp32s3",
            "stable",
            true
        ) == OTA_MANIFEST_OK,
        "explicit rollback policy must permit downgrade"
    );
}

void test_version_comparison() {
    check(ota_version_compare("0.10.0", "0.9.0") > 0, "0.10 > 0.9");
    check(ota_version_compare("0.10.0", "0.10.0") == 0, "equal version");
    check(
        ota_version_compare("0.10.0-rc.1", "0.10.0") < 0,
        "prerelease must sort before release"
    );
    check(
        ota_version_compare("0.10.0+build.2", "0.10.0+build.1") == 0,
        "build metadata must not affect precedence"
    );
}

const char *const PLATFORM_SHA256 =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char *const PLATFORM_SIGNATURE =
    "LxWfXZtUvFcDy7TMZLlUQHu+HKdaN1W3r7mCYlbE4OqV0cBH1wp6Q2tlDr+tpZkM"
    "ouKgUZQ5ryPnmjW6lyksCw==";
const char *const PLATFORM_PUBLIC_KEY =
    "A6EHv/POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg=";
const char *const TRUSTED_KEYRING =
    "{\"release-key-1\":\"A6EHv/POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg=\"}";

void test_platform_release_adapter_maps_real_platform_shape() {
    // Mirrors the platform DeviceRelease projection: `version` instead of
    // `firmware_version`, a real detached signature, and a null publish time.
    const ota_platform_release_t release = {
        .release_id = "rel_2026_10_09_001",
        .version = "0.10.0",
        .hardware_revision = "esp32s3",
        .channel = "stable",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = true,
        .published_at = NULL,
        .min_source_version = NULL,
    };
    ota_manifest_t manifest = {};
    check(
        ota_manifest_from_platform_release(
            &release,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_OK,
        "real platform release projection must adapt"
    );
    check(
        std::strcmp(manifest.firmware_version, "0.10.0") == 0,
        "platform version must map to firmware_version"
    );
    check(
        std::strcmp(manifest.min_source_version, "0.0.0") == 0,
        "absent min source version must default safely"
    );
    check(
        std::strcmp(manifest.published_at, "2026-10-09T00:00:00Z") == 0,
        "absent published_at must use the trusted fallback"
    );
    check(
        std::strcmp(manifest.signature, PLATFORM_SIGNATURE) == 0,
        "detached signature must be preserved verbatim"
    );
    check(
        std::strcmp(manifest.signature, PLATFORM_SHA256) != 0,
        "signature must never be replaced by the artifact hash"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.9.0",
            "esp32s3",
            "stable",
            false
        ) == OTA_MANIFEST_OK,
        "adapted manifest must pass device policy"
    );
    check(
        ota_manifest_verify_signature(&manifest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_OK,
        "platform digest signature must verify on device"
    );
}

void test_platform_release_adapter_fails_closed() {
    const ota_platform_release_t base = {
        .release_id = "rel_2026_10_09_002",
        .version = "0.10.0",
        .hardware_revision = "esp32s3",
        .channel = "stable",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = true,
        .published_at = "2026-10-09T00:00:00Z",
        .min_source_version = "0.9.0",
    };
    ota_manifest_t manifest = {};

    ota_platform_release_t missing_version = base;
    missing_version.version = "";
    check(
        ota_manifest_from_platform_release(
            &missing_version,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_FIRMWARE_VERSION,
        "missing firmware version must fail closed"
    );

    ota_platform_release_t cleartext = base;
    cleartext.artifact_url = "http://download.clarkhub.cn/firmware/sprout.bin";
    check(
        ota_manifest_from_platform_release(
            &cleartext,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_ARTIFACT_URL,
        "cleartext artifact must fail closed"
    );

    ota_platform_release_t bad_hash = base;
    bad_hash.sha256 = "not-a-hash";
    check(
        ota_manifest_from_platform_release(
            &bad_hash,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_SHA256,
        "malformed artifact hash must fail closed"
    );

    ota_platform_release_t zero_size = base;
    zero_size.size_bytes = 0;
    check(
        ota_manifest_from_platform_release(
            &zero_size,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_SIZE,
        "zero artifact size must fail closed"
    );

    ota_platform_release_t bad_channel = base;
    bad_channel.channel = "nightly";
    check(
        ota_manifest_from_platform_release(
            &bad_channel,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_CHANNEL,
        "unsupported channel must fail closed"
    );

    ota_platform_release_t missing_key = base;
    missing_key.signature_key_id = "";
    check(
        ota_manifest_from_platform_release(
            &missing_key,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_MISSING_FIELD,
        "missing signature key id must fail closed"
    );

    ota_platform_release_t missing_signature = base;
    missing_signature.signature = NULL;
    check(
        ota_manifest_from_platform_release(
            &missing_signature,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "absent detached signature must fail closed"
    );

    ota_platform_release_t empty_signature = base;
    empty_signature.signature = "";
    check(
        ota_manifest_from_platform_release(
            &empty_signature,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "empty detached signature must fail closed"
    );

    check(
        ota_manifest_from_platform_release(
            NULL,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_NULL_ARGUMENT,
        "null release must be rejected"
    );
}

void test_detached_signature_fails_closed() {
    const ota_platform_release_t release = {
        .release_id = "rel_2026_10_09_003",
        .version = "0.10.0",
        .hardware_revision = "esp32s3",
        .channel = "stable",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = true,
        .published_at = "2026-10-09T00:00:00Z",
        .min_source_version = "0.9.0",
    };
    ota_manifest_t manifest = {};
    check(
        ota_manifest_from_platform_release(
            &release,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_OK,
        "signed fixture must adapt"
    );
    check(
        ota_manifest_verify_signature(&manifest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_OK,
        "valid signature must pass"
    );

    ota_manifest_t tampered_digest = manifest;
    tampered_digest.sha256[0] =
        (tampered_digest.sha256[0] == '0') ? '1' : '0';
    check(
        ota_manifest_verify_signature(&tampered_digest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "tampered digest must fail"
    );

    ota_manifest_t uppercase_digest = manifest;
    for (size_t index = 0; index < 64; ++index) {
        uppercase_digest.sha256[index] =
            (char)std::toupper((unsigned char)uppercase_digest.sha256[index]);
    }
    check(
        ota_manifest_verify_signature(&uppercase_digest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_OK,
        "digest case must normalize before verification"
    );

    ota_manifest_t tampered_signature = manifest;
    tampered_signature.signature[0] =
        (tampered_signature.signature[0] == 'B') ? 'C' : 'B';
    check(
        ota_manifest_verify_signature(&tampered_signature, TRUSTED_KEYRING) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "tampered signature must fail"
    );

    ota_manifest_t unknown_key = manifest;
    std::strcpy(unknown_key.signature_key_id, "release-key-2");
    check(
        ota_manifest_verify_signature(&unknown_key, TRUSTED_KEYRING) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "unknown key id must fail"
    );

    check(
        ota_manifest_verify_signature(&manifest, "{}") ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "empty keyring must fail closed"
    );
    check(
        ota_manifest_verify_signature(&manifest, "") ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "missing keyring must fail closed"
    );

    const std::string malformed_keyring =
        "{\"release-key-1\":\"" +
        std::string(PLATFORM_PUBLIC_KEY) +
        "\",\"note\":\"}";
    check(
        ota_manifest_verify_signature(
            &manifest,
            malformed_keyring.c_str()
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "malformed keyring must fail closed"
    );

    const std::string embedded_keyring =
        "{\"metadata\":\"release-key-1\",\"release-key-1\":\"" +
        std::string(PLATFORM_PUBLIC_KEY) + "\"}";
    check(
        ota_manifest_verify_signature(
            &manifest,
            embedded_keyring.c_str()
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "key ids inside other fields must not be treated as entries"
    );

    const std::string duplicate_keyring =
        "{\"release-key-1\":\"" +
        std::string(PLATFORM_PUBLIC_KEY) +
        "\",\"release-key-1\":\"" +
        std::string(PLATFORM_PUBLIC_KEY) + "\"}";
    check(
        ota_manifest_verify_signature(
            &manifest,
            duplicate_keyring.c_str()
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "duplicate keyring entries must fail closed"
    );

    const std::string escaped_keyring =
        "{\"release-key\\u002d1\":\"" +
        std::string(PLATFORM_PUBLIC_KEY) + "\"}";
    check(
        ota_manifest_verify_signature(
            &manifest,
            escaped_keyring.c_str()
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "escaped key ids must fail closed"
    );

    ota_manifest_t unpadded_signature = manifest;
    unpadded_signature.signature[
        std::strlen(unpadded_signature.signature) - 1
    ] = '\0';
    check(
        ota_manifest_verify_signature(
            &unpadded_signature,
            TRUSTED_KEYRING
        ) == OTA_MANIFEST_ERR_SIGNATURE,
        "non-canonical Base64 must fail"
    );
}

}  // namespace

int main() {
    test_valid_manifest_parses();
    test_missing_required_field_is_rejected();
    test_duplicate_field_is_rejected();
    test_unsupported_schema_is_rejected();
    test_cleartext_artifact_is_rejected();
    test_sha256_validation();
    test_signature_fields_fail_closed();
    test_device_policy_rejects_mismatches();
    test_anti_downgrade_and_rollback_policy();
    test_version_comparison();
    test_platform_release_adapter_maps_real_platform_shape();
    test_platform_release_adapter_fails_closed();
    test_detached_signature_fails_closed();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota manifest tests passed");
    return 0;
}
