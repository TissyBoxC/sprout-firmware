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
    "\"schema_version\":\"1.0.0\","
    "\"release_id\":\"rel_2026_10_09_001\","
    "\"firmware_version\":\"0.10.0\","
    "\"hardware_revision\":\"esp32s3\","
    "\"channel\":\"stable\","
    "\"artifact_key\":\"firmware/sprout.bin\","
    "\"artifact_url\":\"https://download.clarkhub.cn/firmware/sprout.bin\","
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
    "\"size_bytes\":1048576,"
    "\"signature_key_id\":\"release-key-1\","
    "\"signature_algorithm\":\"ed25519\","
    "\"signature_payload_version\":\"2\","
    "\"signature\":\"PX7gAyWvtKu01SKOP2j4mRuoGcz+741zWZ22lY6+x95u5852OuczXoX2jFdVSZseVe5/CyLDuJGtJtmYXe3xDA==\","
    "\"rollback_allowed\":true,"
    "\"mandatory\":true,"
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
        std::strcmp(manifest.release_id, "rel_2026_10_09_001") == 0,
        "release id must round-trip"
    );
    check(manifest.size_bytes == 1048576, "size must round-trip");
    check(manifest.rollback_allowed, "rollback flag must round-trip");
    check(manifest.mandatory, "mandatory flag must round-trip");
    check(
        std::strcmp(manifest.artifact_key, "firmware/sprout.bin") == 0,
        "artifact key must round-trip"
    );
    check(
        std::strcmp(
            manifest.artifact_url,
            "https://download.clarkhub.cn/firmware/sprout.bin"
        ) == 0,
        "artifact URL must round-trip"
    );
}

void test_missing_required_field_is_rejected() {
    const char *json =
        "{\"schema_version\":\"1.0.0\",\"release_id\":\"r1\"}";
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
        "\"schema_version\":\"1.0.0\","
        "\"schema_version\":\"1.0.0\","
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
    char *entry = std::strstr(json, "\"schema_version\":\"1.0.0\"");
    check(entry != nullptr, "schema field must be present");
    entry[20] = '9';
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

void test_artifact_size_matches_partition_capacity() {
    std::string json(VALID_MANIFEST);
    const std::string size_entry = "\"size_bytes\":1048576";
    const std::string maximum_size_entry =
        "\"size_bytes\":" + std::to_string(
            OTA_MANIFEST_MAX_ARTIFACT_SIZE_BYTES
        );
    const size_t size_index = json.find(size_entry);
    check(size_index != std::string::npos, "artifact size field must exist");
    if (size_index == std::string::npos) {
        return;
    }
    json.replace(size_index, size_entry.size(), maximum_size_entry);
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(json.c_str(), json.size(), &manifest) ==
            OTA_MANIFEST_OK,
        "4 MiB artifact must be accepted"
    );
    check(
        manifest.size_bytes == OTA_MANIFEST_MAX_ARTIFACT_SIZE_BYTES,
        "maximum artifact size must round-trip"
    );

    json.replace(
        size_index,
        maximum_size_entry.size(),
        "\"size_bytes\":" + std::to_string(
            OTA_MANIFEST_MAX_ARTIFACT_SIZE_BYTES + 1
        )
    );
    check(
        ota_manifest_parse(json.c_str(), json.size(), &manifest) ==
            OTA_MANIFEST_ERR_SIZE,
        "artifact over 4 MiB must be rejected before installation"
    );
}

void test_artifact_url_matches_device_buffer_and_semantics() {
    const char *const prefix = "https://download.clarkhub.cn/";
    const char *const suffix = "firmware/sprout.bin";
    const size_t prefix_size = std::strlen(prefix);
    const size_t suffix_size = std::strlen(suffix);
    const size_t maximum_url_size = 512;
    check(
        maximum_url_size >= prefix_size + suffix_size,
        "maximum URL must fit the required path"
    );
    std::string maximum_url(prefix);
    maximum_url.append(
        maximum_url_size - prefix_size - suffix_size - 1,
        'a'
    );
    maximum_url.append("/");
    maximum_url.append(suffix);
    check(
        maximum_url.size() == maximum_url_size,
        "fixture must use the exact 512-byte boundary"
    );
    std::string json(VALID_MANIFEST);
    const std::string url_entry =
        "\"artifact_url\":\"https://download.clarkhub.cn/firmware/sprout.bin\"";
    const size_t url_index = json.find(url_entry);
    check(url_index != std::string::npos, "artifact URL field must exist");
    if (url_index == std::string::npos) {
        return;
    }
    const std::string maximum_url_entry =
        std::string("\"artifact_url\":\"") + maximum_url + "\"";
    json.replace(url_index, url_entry.size(), maximum_url_entry);
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(json.c_str(), json.size(), &manifest) ==
            OTA_MANIFEST_OK,
        "512-byte artifact URL must be accepted"
    );
    check(
        std::strlen(manifest.artifact_url) == maximum_url_size,
        "maximum URL must round-trip without truncation"
    );

    maximum_url.insert(maximum_url.size() - suffix_size, "a");
    check(
        maximum_url.size() == maximum_url_size + 1,
        "over-limit fixture must grow by exactly one byte"
    );
    check(
        maximum_url.size() == 513,
        "over-limit fixture must be exactly 513 bytes"
    );
    const std::string oversized_url_entry =
        std::string("\"artifact_url\":\"") + maximum_url + "\"";
    json.replace(url_index, maximum_url_entry.size(), oversized_url_entry);
    const ota_manifest_error_t oversized_result = ota_manifest_parse(
        json.c_str(),
        json.size(),
        &manifest
    );
    check(
        oversized_result == OTA_MANIFEST_ERR_ARTIFACT_URL,
        "513-byte artifact URL must be rejected"
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

void test_artifact_key_and_url_contract() {
    ota_manifest_t manifest = {};
    check(
        ota_manifest_parse(
            VALID_MANIFEST,
            std::strlen(VALID_MANIFEST),
            &manifest
        ) == OTA_MANIFEST_OK,
        "artifact fixture must parse"
    );

    const char *const invalid_keys[] = {
        "/absolute.bin",
        "firmware/../sprout.bin",
        "firmware//sprout.bin",
    };
    for (const char *invalid_key : invalid_keys) {
        char json[2048] = {};
        std::strcpy(json, VALID_MANIFEST);
        char *key_entry = std::strstr(
            json,
            "\"artifact_key\":\"firmware/sprout.bin\""
        );
        check(key_entry != nullptr, "artifact key field must be present");
        if (key_entry == nullptr) {
            continue;
        }
        const std::string key_json =
            std::string("\"artifact_key\":\"") + invalid_key + "\"";
        std::string replaced(json);
        replaced.replace(
            static_cast<size_t>(key_entry - json),
            std::strlen("\"artifact_key\":\"firmware/sprout.bin\""),
            key_json
        );
        check(
            ota_manifest_parse(
                replaced.c_str(),
                replaced.size(),
                &manifest
            ) == OTA_MANIFEST_ERR_ARTIFACT_KEY ||
            ota_manifest_parse(
                replaced.c_str(),
                replaced.size(),
                &manifest
            ) == OTA_MANIFEST_ERR_ARTIFACT_URL,
            invalid_key
        );
    }

    const char *const invalid_urls[] = {
        "https://download.clarkhub.cn/firmware/sprout.bin?token=1",
        "https://download.clarkhub.cn/firmware/sprout.bin#fragment",
        "https://download.clarkhub.cn/firmware/%73prout.bin",
        "https://download.clarkhub.cn/firmware/../sprout.bin",
        "https://download.clarkhub.cn/firmware//sprout.bin",
        "https://user@download.clarkhub.cn/firmware/sprout.bin",
        "https://user:password@download.clarkhub.cn/firmware/sprout.bin",
        "https:///firmware/sprout.bin",
        "https://download.clarkhub.cn/firmware/ sprout.bin",
        "https://download.clarkhub.cn/firmware/",
        "https://download.clarkhub.cn/",
    };
    for (const char *invalid_url : invalid_urls) {
        char json[2048] = {};
        std::strcpy(json, VALID_MANIFEST);
        char *url_entry = std::strstr(
            json,
            "\"artifact_url\":\"https://download.clarkhub.cn/firmware/sprout.bin\""
        );
        check(url_entry != nullptr, "artifact URL fixture must be present");
        if (url_entry == nullptr) {
            continue;
        }
        const std::string replacement =
            std::string("\"artifact_url\":\"") + invalid_url + "\"";
        std::string replaced(json);
        replaced.replace(
            static_cast<size_t>(url_entry - json),
            std::strlen(
                "\"artifact_url\":\"https://download.clarkhub.cn/firmware/sprout.bin\""
            ),
            replacement
        );
        check(
            ota_manifest_parse(
                replaced.c_str(),
                replaced.size(),
                &manifest
            ) == OTA_MANIFEST_ERR_ARTIFACT_URL,
            invalid_url
        );
    }

    const char *const valid_at_path_url =
        "https://download.clarkhub.cn/firmware/release@2026/sprout.bin";
    std::string at_path_json(VALID_MANIFEST);
    const std::string original_key =
        "\"artifact_key\":\"firmware/sprout.bin\"";
    const std::string at_path_key =
        "\"artifact_key\":\"firmware/release@2026/sprout.bin\"";
    const size_t key_index = at_path_json.find(original_key);
    check(key_index != std::string::npos, "artifact key must exist");
    if (key_index != std::string::npos) {
        at_path_json.replace(key_index, original_key.size(), at_path_key);
    }
    const std::string original_url =
        "\"artifact_url\":\"https://download.clarkhub.cn/firmware/sprout.bin\"";
    const std::string at_path_url =
        std::string("\"artifact_url\":\"") + valid_at_path_url + "\"";
    const size_t url_index = at_path_json.find(original_url);
    check(url_index != std::string::npos, "artifact URL must exist");
    if (url_index != std::string::npos) {
        at_path_json.replace(url_index, original_url.size(), at_path_url);
        check(
            ota_manifest_parse(
                at_path_json.c_str(),
                at_path_json.size(),
                &manifest
            ) == OTA_MANIFEST_OK,
            "at-sign in a path segment must not be mistaken for userinfo"
        );
        check(
            std::strcmp(
                manifest.artifact_url,
                valid_at_path_url
            ) == 0,
            "at-sign in a path must survive artifact key matching"
        );
    }
}

void test_payload_version_and_mandatory_are_required() {
    const char *const removed_fields[] = {
        "\"artifact_key\":\"firmware/sprout.bin\",",
        "\"signature_payload_version\":\"2\",",
        "\"mandatory\":true,",
    };
    for (const char *removed_field : removed_fields) {
        std::string json(VALID_MANIFEST);
        const size_t index = json.find(removed_field);
        check(index != std::string::npos, "required security field must exist");
        if (index == std::string::npos) {
            continue;
        }
        json.erase(index, std::strlen(removed_field));
        ota_manifest_t manifest = {};
        check(
            ota_manifest_parse(json.c_str(), json.size(), &manifest) ==
                OTA_MANIFEST_ERR_MISSING_FIELD,
            "absent signed security field must be rejected"
        );
    }

    std::string wrong_version(VALID_MANIFEST);
    const size_t version_index = wrong_version.find(
        "\"signature_payload_version\":\"2\""
    );
    check(version_index != std::string::npos, "payload version must exist");
    if (version_index != std::string::npos) {
        wrong_version.replace(
            version_index,
            std::strlen("\"signature_payload_version\":\"2\""),
            "\"signature_payload_version\":\"1\""
        );
        ota_manifest_t manifest = {};
        check(
            ota_manifest_parse(
                wrong_version.c_str(),
                wrong_version.size(),
                &manifest
            ) == OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION,
            "unsupported signature payload version must be explicit"
        );
    }
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
    manifest.rollback_allowed = false;
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
            false
        ) == OTA_MANIFEST_ERR_ROLLBACK_NOT_ALLOWED,
        "allow_downgrade=false must reject even when the manifest allows rollback"
    );
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.11.0",
            "esp32s3",
            "stable",
            true
        ) == OTA_MANIFEST_OK,
        "downgrade requires both allow_downgrade and rollback_allowed"
    );
    manifest.rollback_allowed = false;
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
    "50IR36ZPyidQNAefjdW1lFUqMVbvcoZ5f6Kt+yghQAK+Z1IlauZINfH5aYazbt+BQ"
    "FcPKxqXfSqNLlqGIyAaCQ==";
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
        .artifact_key = "firmware/sprout.bin",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature_payload_version = "2",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = false,
        .mandatory = true,
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
    check(manifest.mandatory, "mandatory policy must adapt");
    check(
        ota_manifest_validate_device(
            &manifest,
            "0.9.0",
            "esp32s3",
            "stable",
            true
        ) == OTA_MANIFEST_OK,
        "adapted manifest must pass device policy"
    );
    check(
        ota_manifest_verify_signature(&manifest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "a defaulted unsigned minimum source must not verify as signed policy"
    );
}

void test_platform_release_adapter_preserves_signed_min_source_version() {
    const ota_platform_release_t release = {
        .release_id = "rel_2026_10_09_001",
        .version = "0.10.0",
        .hardware_revision = "esp32s3",
        .channel = "stable",
        .artifact_key = "firmware/sprout.bin",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature_payload_version = "2",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = false,
        .mandatory = true,
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
        "platform release with a signed minimum source must adapt"
    );
    check(
        std::strcmp(manifest.min_source_version, "0.9.0") == 0,
        "adapter must preserve the signed minimum source version"
    );
    check(
        ota_manifest_verify_signature(&manifest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_OK,
        "platform-provided signed minimum source must verify"
    );
}

void test_platform_release_adapter_fails_closed() {
    const ota_platform_release_t base = {
        .release_id = "rel_2026_10_09_002",
        .version = "0.10.0",
        .hardware_revision = "esp32s3",
        .channel = "stable",
        .artifact_key = "firmware/sprout.bin",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature_payload_version = "2",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = true,
        .mandatory = true,
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

    ota_platform_release_t mismatched_key = base;
    mismatched_key.artifact_key = "firmware/other.bin";
    check(
        ota_manifest_from_platform_release(
            &mismatched_key,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_ARTIFACT_URL,
        "artifact key and URL must describe the same artifact"
    );

    ota_platform_release_t missing_artifact_key = base;
    missing_artifact_key.artifact_key = NULL;
    check(
        ota_manifest_from_platform_release(
            &missing_artifact_key,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_MISSING_FIELD,
        "absent artifact key must fail closed"
    );

    ota_platform_release_t unsupported_payload = base;
    unsupported_payload.signature_payload_version = "1";
    check(
        ota_manifest_from_platform_release(
            &unsupported_payload,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION,
        "unsupported signature payload version must fail closed"
    );

    ota_platform_release_t missing_payload = base;
    missing_payload.signature_payload_version = NULL;
    check(
        ota_manifest_from_platform_release(
            &missing_payload,
            "2026-10-09T00:00:00Z",
            &manifest
        ) == OTA_MANIFEST_ERR_MISSING_FIELD,
        "absent signature payload version must fail closed"
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
        .release_id = "rel_2026_10_09_001",
        .version = "0.10.0",
        .hardware_revision = "esp32s3",
        .channel = "stable",
        .artifact_key = "firmware/sprout.bin",
        .artifact_url = "https://download.clarkhub.cn/firmware/sprout.bin",
        .sha256 = PLATFORM_SHA256,
        .size_bytes = 1048576,
        .signature_key_id = "release-key-1",
        .signature_algorithm = "ed25519",
        .signature_payload_version = "2",
        .signature = PLATFORM_SIGNATURE,
        .rollback_allowed = false,
        .mandatory = true,
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
        std::strcmp(manifest.release_id, release.release_id) == 0 &&
            std::strcmp(manifest.firmware_version, release.version) == 0 &&
            std::strcmp(
                manifest.hardware_revision,
                release.hardware_revision
            ) == 0 &&
            std::strcmp(manifest.channel, release.channel) == 0 &&
            std::strcmp(manifest.artifact_key, release.artifact_key) == 0 &&
            std::strcmp(manifest.artifact_url, release.artifact_url) == 0 &&
            std::strcmp(manifest.sha256, release.sha256) == 0 &&
            manifest.size_bytes == release.size_bytes &&
            std::strcmp(
                manifest.signature_key_id,
                release.signature_key_id
            ) == 0 &&
            std::strcmp(
                manifest.signature_algorithm,
                release.signature_algorithm
            ) == 0 &&
            std::strcmp(
                manifest.signature_payload_version,
                release.signature_payload_version
            ) == 0 &&
            std::strcmp(manifest.signature, release.signature) == 0 &&
            manifest.rollback_allowed == release.rollback_allowed &&
            manifest.mandatory == release.mandatory &&
            std::strcmp(manifest.min_source_version, "0.9.0") == 0,
        "platform projection must preserve every signed policy field"
    );
    check(
        ota_manifest_verify_signature(&manifest, TRUSTED_KEYRING) ==
            OTA_MANIFEST_OK,
        "valid signature must pass"
    );

    struct SignedPolicyTamper {
        const char *name;
        void (*apply)(ota_manifest_t *manifest);
    };
    const SignedPolicyTamper policy_tampers[] = {
        {"release_id", [](ota_manifest_t *value) {
            std::strcpy(value->release_id, "rel_2026_10_09_002");
        }},
        {"firmware_version", [](ota_manifest_t *value) {
            std::strcpy(value->firmware_version, "0.10.1");
        }},
        {"hardware_revision", [](ota_manifest_t *value) {
            std::strcpy(value->hardware_revision, "esp32c3");
        }},
        {"channel", [](ota_manifest_t *value) {
            std::strcpy(value->channel, "canary");
        }},
        {"artifact_key", [](ota_manifest_t *value) {
            std::strcpy(value->artifact_key, "firmware/other.bin");
        }},
        {"artifact_url", [](ota_manifest_t *value) {
            std::strcpy(
                value->artifact_url,
                "https://download.clarkhub.cn/artifacts/firmware/sprout.bin"
            );
        }},
        {"sha256", [](ota_manifest_t *value) {
            value->sha256[0] = '1';
        }},
        {"size_bytes", [](ota_manifest_t *value) {
            value->size_bytes = 1048577;
        }},
        {"signature_algorithm", [](ota_manifest_t *value) {
            std::strcpy(value->signature_algorithm, "Ed25519");
        }},
        {"rollback_allowed", [](ota_manifest_t *value) {
            value->rollback_allowed = true;
        }},
        {"mandatory", [](ota_manifest_t *value) {
            value->mandatory = false;
        }},
        {"min_source_version", [](ota_manifest_t *value) {
            std::strcpy(value->min_source_version, "0.9.1");
        }},
    };
    for (const SignedPolicyTamper &tamper : policy_tampers) {
        ota_manifest_t tampered = manifest;
        tamper.apply(&tampered);
        check(
            ota_manifest_verify_signature(&tampered, TRUSTED_KEYRING) ==
                OTA_MANIFEST_ERR_SIGNATURE,
            tamper.name
        );
    }

    ota_manifest_t unsigned_publish_time = manifest;
    std::strcpy(
        unsigned_publish_time.published_at,
        "2026-10-10T00:00:00Z"
    );
    check(
        ota_manifest_verify_signature(
            &unsigned_publish_time,
            TRUSTED_KEYRING
        ) == OTA_MANIFEST_OK,
        "published_at must remain unsigned audit metadata"
    );

    ota_manifest_t tampered_key_id = manifest;
    std::strcpy(tampered_key_id.signature_key_id, "release-key-2");
    check(
        ota_manifest_verify_signature(&tampered_key_id, TRUSTED_KEYRING) ==
            OTA_MANIFEST_ERR_SIGNATURE,
        "signature_key_id tampering must not select an unknown trusted key"
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
        if (uppercase_digest.sha256[index] >= 'a' &&
            uppercase_digest.sha256[index] <= 'f') {
            uppercase_digest.sha256[index] = (char)(
                uppercase_digest.sha256[index] - 'a' + 'A'
            );
        }
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
    test_artifact_size_matches_partition_capacity();
    test_artifact_url_matches_device_buffer_and_semantics();
    test_sha256_validation();
    test_signature_fields_fail_closed();
    test_artifact_key_and_url_contract();
    test_payload_version_and_mandatory_are_required();
    test_device_policy_rejects_mismatches();
    test_anti_downgrade_and_rollback_policy();
    test_version_comparison();
    test_platform_release_adapter_maps_real_platform_shape();
    test_platform_release_adapter_preserves_signed_min_source_version();
    test_platform_release_adapter_fails_closed();
    test_detached_signature_fails_closed();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota manifest tests passed");
    return 0;
}
