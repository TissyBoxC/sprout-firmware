#include "ota_manifest.h"

#include <cstdio>
#include <cstring>

extern "C" const char *const sprout_test_release_id;
extern "C" const char *const sprout_test_firmware_version;
extern "C" const char *const sprout_test_hardware_revision;
extern "C" const char *const sprout_test_channel;
extern "C" const char *const sprout_test_artifact_key;
extern "C" const char *const sprout_test_artifact_url;
extern "C" const char *const sprout_test_sha256;
extern "C" const unsigned long long sprout_test_size_bytes;
extern "C" const char *const sprout_test_signature_key_id;
extern "C" const char *const sprout_test_signature_algorithm;
extern "C" const char *const sprout_test_signature_payload_version;
extern "C" const char *const sprout_test_signature;
extern "C" const bool sprout_test_rollback_allowed;
extern "C" const bool sprout_test_mandatory;
extern "C" const char *const sprout_test_published_at;
extern "C" const char *const sprout_test_min_source_version;
extern "C" const char *const sprout_test_keyring;

int main() {
    const ota_platform_release_t release = {
        .release_id = sprout_test_release_id,
        .version = sprout_test_firmware_version,
        .hardware_revision = sprout_test_hardware_revision,
        .channel = sprout_test_channel,
        .artifact_key = sprout_test_artifact_key,
        .artifact_url = sprout_test_artifact_url,
        .sha256 = sprout_test_sha256,
        .size_bytes = sprout_test_size_bytes,
        .signature_key_id = sprout_test_signature_key_id,
        .signature_algorithm = sprout_test_signature_algorithm,
        .signature_payload_version =
            sprout_test_signature_payload_version,
        .signature = sprout_test_signature,
        .rollback_allowed = sprout_test_rollback_allowed,
        .mandatory = sprout_test_mandatory,
        .published_at = sprout_test_published_at,
        .min_source_version = sprout_test_min_source_version,
    };
    ota_manifest_t manifest = {};
    const ota_manifest_error_t adapted = ota_manifest_from_platform_release(
        &release,
        "2026-10-17T00:00:00Z",
        &manifest
    );
    if (adapted != OTA_MANIFEST_OK) {
        std::fprintf(
            stderr,
            "manifest adaptation failed: %d\n",
            static_cast<int>(adapted)
        );
        return 1;
    }
    if (ota_manifest_verify_signature(&manifest, sprout_test_keyring) !=
        OTA_MANIFEST_OK) {
        std::fputs("manifest signature verification failed\n", stderr);
        return 2;
    }
    if (!ota_manifest_sha256_matches(manifest.sha256, sprout_test_sha256)) {
        std::fputs("manifest digest mismatch\n", stderr);
        return 3;
    }
    if (manifest.mandatory != sprout_test_mandatory ||
        std::strcmp(manifest.artifact_key, sprout_test_artifact_key) != 0 ||
        std::strcmp(
            manifest.signature_payload_version,
            sprout_test_signature_payload_version
        ) != 0) {
        std::fputs("manifest policy projection mismatch\n", stderr);
        return 4;
    }
    manifest.sha256[0] = manifest.sha256[0] == 'a' ? 'b' : 'a';
    if (ota_manifest_verify_signature(&manifest, sprout_test_keyring) !=
        OTA_MANIFEST_ERR_SIGNATURE) {
        std::fputs("tampered manifest digest was accepted\n", stderr);
        return 5;
    }
    manifest = {};
    if (ota_manifest_from_platform_release(
            &release,
            "2026-10-17T00:00:00Z",
            &manifest
        ) != OTA_MANIFEST_OK) {
        std::fputs("manifest restoration failed\n", stderr);
        return 6;
    }
    std::strcpy(manifest.published_at, "2026-10-18T00:00:00Z");
    if (ota_manifest_verify_signature(&manifest, sprout_test_keyring) !=
        OTA_MANIFEST_OK) {
        std::fputs("unsigned publish time changed the signature result\n", stderr);
        return 7;
    }
    std::puts("signed firmware manifest verification passed");
    return 0;
}
