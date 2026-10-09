#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANIFEST_SCHEMA_VERSION "1"
#define OTA_MANIFEST_RELEASE_ID_SIZE 65
#define OTA_MANIFEST_VERSION_SIZE 32
#define OTA_MANIFEST_HARDWARE_SIZE 32
#define OTA_MANIFEST_CHANNEL_SIZE 16
#define OTA_MANIFEST_URL_SIZE 512
#define OTA_MANIFEST_HASH_SIZE 65
#define OTA_MANIFEST_KEY_ID_SIZE 65
#define OTA_MANIFEST_SIGNATURE_SIZE 513
#define OTA_MANIFEST_TIME_SIZE 32

/** @brief Stable validation errors shared by firmware OTA paths. */
typedef enum {
    OTA_MANIFEST_OK = 0,
    OTA_MANIFEST_ERR_NULL_ARGUMENT,
    OTA_MANIFEST_ERR_JSON,
    OTA_MANIFEST_ERR_SCHEMA_VERSION,
    OTA_MANIFEST_ERR_RELEASE_ID,
    OTA_MANIFEST_ERR_FIRMWARE_VERSION,
    OTA_MANIFEST_ERR_HARDWARE_REVISION,
    OTA_MANIFEST_ERR_CHANNEL,
    OTA_MANIFEST_ERR_ARTIFACT_URL,
    OTA_MANIFEST_ERR_SHA256,
    OTA_MANIFEST_ERR_SIZE,
    OTA_MANIFEST_ERR_SIGNATURE,
    OTA_MANIFEST_ERR_PUBLISHED_AT,
    OTA_MANIFEST_ERR_MIN_SOURCE_VERSION,
    OTA_MANIFEST_ERR_DUPLICATE_FIELD,
    OTA_MANIFEST_ERR_MISSING_FIELD,
    OTA_MANIFEST_ERR_UNSUPPORTED_FIELD,
    OTA_MANIFEST_ERR_VERSION_ORDER,
    OTA_MANIFEST_ERR_VERSION_TOO_OLD,
    OTA_MANIFEST_ERR_ROLLBACK_NOT_ALLOWED,
} ota_manifest_error_t;

/** @brief Immutable firmware release manifest.
 *
 * The structure is intentionally fixed-size so a malformed or hostile
 * platform response cannot trigger unbounded allocation on the device.
 */
typedef struct {
    char schema_version[OTA_MANIFEST_VERSION_SIZE];
    char release_id[OTA_MANIFEST_RELEASE_ID_SIZE];
    char firmware_version[OTA_MANIFEST_VERSION_SIZE];
    char hardware_revision[OTA_MANIFEST_HARDWARE_SIZE];
    char channel[OTA_MANIFEST_CHANNEL_SIZE];
    char artifact_url[OTA_MANIFEST_URL_SIZE];
    char sha256[OTA_MANIFEST_HASH_SIZE];
    uint64_t size_bytes;
    char signature_key_id[OTA_MANIFEST_KEY_ID_SIZE];
    char signature[OTA_MANIFEST_SIGNATURE_SIZE];
    bool rollback_allowed;
    char published_at[OTA_MANIFEST_TIME_SIZE];
    char min_source_version[OTA_MANIFEST_VERSION_SIZE];
} ota_manifest_t;

/** @brief Platform device-release projection used by the firmware adapter.
 *
 * The platform management API returns a device release with `version` (not
 * `firmware_version`) plus extra operator-only fields. The adapter maps that
 * projection onto the strict on-device manifest and fails closed unless every
 * security-relevant field, including the detached `signature`, is present.
 */
typedef struct {
    const char *release_id;
    const char *version;
    const char *hardware_revision;
    const char *channel;
    const char *artifact_url;
    const char *sha256;
    uint64_t size_bytes;
    const char *signature_key_id;
    const char *signature;
    bool rollback_allowed;
    const char *published_at;
    const char *min_source_version;
} ota_platform_release_t;

/** @brief Parse one flat OTA manifest JSON object. */
ota_manifest_error_t ota_manifest_parse(
    const char *json,
    size_t json_size,
    ota_manifest_t *manifest_out
);

/** @brief Normalize a platform device release into the strict manifest.
 *
 * Only the non-security metadata is defaulted: an absent min source version
 * becomes "0.0.0" and an absent publish time becomes `fallback_published_at`.
 * The detached `signature` and `signature_key_id` are mandatory security
 * fields and are never substituted with the artifact hash. The function fails
 * closed on any missing or malformed security field.
 */
ota_manifest_error_t ota_manifest_from_platform_release(
    const ota_platform_release_t *release,
    const char *fallback_published_at,
    ota_manifest_t *manifest_out
);

/** @brief Return the stable name for one manifest error. */
const char *ota_manifest_error_name(ota_manifest_error_t error);

/** @brief Compare two dotted numeric versions with optional suffix. */
int ota_version_compare(const char *left, const char *right);

/** @brief Validate a manifest against the running device profile.
 *
 * Fails closed when the hardware, channel, minimum source version, or
 * anti-downgrade rule does not match.
 */
ota_manifest_error_t ota_manifest_validate_device(
    const ota_manifest_t *manifest,
    const char *current_firmware_version,
    const char *hardware_revision,
    const char *channel,
    bool allow_downgrade
);

/** @brief Return true when a hex SHA-256 is syntactically valid. */
bool ota_manifest_sha256_is_valid(const char *sha256);

/** @brief Compare the expected and actual SHA-256 hex strings. */
bool ota_manifest_sha256_matches(
    const char *expected,
    const char *actual
);

#ifdef __cplusplus
}
#endif
