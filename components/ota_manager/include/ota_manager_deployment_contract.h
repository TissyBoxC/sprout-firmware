#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANAGER_RELEASE_ID_SIZE 65
#define OTA_MANAGER_DEPLOYMENT_ID_SIZE 65

typedef enum {
    OTA_MANAGER_DEPLOYMENT_KIND_UNKNOWN = 0,
    OTA_MANAGER_DEPLOYMENT_KIND_INSTALL,
    OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK,
} ota_manager_deployment_kind_t;

/** @brief Deployment context returned with the device update projection. */
typedef struct {
    char status[32];
    char current_version[32];
    char target_version[32];
    char deployment_id[OTA_MANAGER_DEPLOYMENT_ID_SIZE];
    char rollback_release_id[OTA_MANAGER_RELEASE_ID_SIZE];
    char rollback_of_deployment_id[OTA_MANAGER_DEPLOYMENT_ID_SIZE];
    ota_manager_deployment_kind_t kind;
    bool update_available;
    bool rollback_available;
} ota_manager_deployment_context_t;

typedef enum {
    OTA_MANAGER_DEPLOYMENT_OK = 0,
    OTA_MANAGER_DEPLOYMENT_MISSING_FIELD,
    OTA_MANAGER_DEPLOYMENT_CONTRADICTORY,
    OTA_MANAGER_DEPLOYMENT_ROLLBACK_NOT_ALLOWED,
} ota_manager_deployment_error_t;

/**
 * @brief Validate the platform update projection without trusting formatting.
 *
 * The response must fit the fixed device buffers exactly. Truncating a
 * deployment or release identifier could bind an event to another command.
 */
static inline bool ota_manager_copy_exact_platform_field(
    const char *source,
    char *destination,
    size_t destination_size
) {
    if (source == NULL || destination == NULL || destination_size == 0) {
        return false;
    }
    const size_t source_length = strlen(source);
    if (source_length >= destination_size) {
        destination[0] = '\0';
        return false;
    }
    memcpy(destination, source, source_length + 1);
    return true;
}

static inline bool ota_manager_is_lower_snake_identifier(
    const char *value,
    size_t maximum_length
) {
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    size_t length = 0;
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if (++length > maximum_length ||
            !((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') ||
              *cursor == '_')) {
            return false;
        }
    }
    return value[0] >= 'a' && value[0] <= 'z';
}

static inline bool ota_manager_string_is_nonempty(const char *value) {
    return value != NULL && value[0] != '\0';
}

static inline void ota_manager_format_idempotency_key(
    const char *deployment_id,
    const char *release_id,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (!ota_manager_string_is_nonempty(deployment_id) ||
        !ota_manager_string_is_nonempty(release_id)) {
        return;
    }
    const size_t deployment_length = strlen(deployment_id);
    const size_t release_length = strlen(release_id);
    if (deployment_length + 1 + release_length + 1 > output_size) {
        return;
    }
    memcpy(output, deployment_id, deployment_length);
    output[deployment_length] = '|';
    memcpy(
        output + deployment_length + 1,
        release_id,
        release_length
    );
    output[deployment_length + 1 + release_length] = '\0';
}

static inline bool ota_manager_transaction_identity_matches(
    const char *deployment_id,
    const char *release_id,
    const char *other_deployment_id,
    const char *other_release_id
) {
    return ota_manager_string_is_nonempty(deployment_id) &&
        ota_manager_string_is_nonempty(release_id) &&
        other_deployment_id != NULL &&
        other_release_id != NULL &&
        strcmp(deployment_id, other_deployment_id) == 0 &&
        strcmp(release_id, other_release_id) == 0;
}

static inline bool ota_manager_transaction_kind_is_valid(
    ota_manager_deployment_kind_t kind
) {
    return kind == OTA_MANAGER_DEPLOYMENT_KIND_INSTALL ||
        kind == OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK;
}

static inline ota_manager_deployment_kind_t
ota_manager_deployment_kind_from_protocol(const char *deployment_kind) {
    if (deployment_kind != NULL && strcmp(deployment_kind, "install") == 0) {
        return OTA_MANAGER_DEPLOYMENT_KIND_INSTALL;
    }
    if (deployment_kind != NULL && strcmp(deployment_kind, "rollback") == 0) {
        return OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK;
    }
    return OTA_MANAGER_DEPLOYMENT_KIND_UNKNOWN;
}

/**
 * @brief Decide whether a newly fetched deployment may take over persisted state.
 *
 * A matching deployment/release pair is a replay. An outdated deployment for
 * the same release must not replace a newer transaction. There is deliberately
 * no timestamp ordering; deployment IDs are opaque and are never compared.
 */
static inline bool ota_manager_deployment_can_replace(
    const ota_manager_deployment_context_t *candidate,
    const char *candidate_release_id,
    const char *persisted_deployment_id,
    const char *persisted_release_id,
    bool has_unfinished_work
) {
    if (candidate == NULL ||
        !ota_manager_string_is_nonempty(candidate_release_id)) {
        return false;
    }
    if (!has_unfinished_work) {
        return true;
    }
    return ota_manager_transaction_identity_matches(
        candidate->deployment_id,
        candidate_release_id,
        persisted_deployment_id,
        persisted_release_id
    );
}

/**
 * @brief Validate the deployment fields that authorize one OTA transaction.
 *
 * Rollback is never inferred from version ordering. It must be explicitly
 * declared by the platform and must agree with the release projection.
 */
static inline ota_manager_deployment_error_t
ota_manager_validate_deployment_context(
    const ota_manager_deployment_context_t *context,
    const char *release_id,
    const char *release_version,
    bool release_rollback_allowed
) {
    if (context == NULL ||
        !ota_manager_string_is_nonempty(release_id) ||
        !ota_manager_string_is_nonempty(release_version) ||
        !ota_manager_is_lower_snake_identifier(context->status, 31) ||
        !ota_manager_string_is_nonempty(context->current_version) ||
        !ota_manager_string_is_nonempty(context->target_version) ||
        !ota_manager_is_lower_snake_identifier(
            context->deployment_id,
            OTA_MANAGER_DEPLOYMENT_ID_SIZE - 1
        )) {
        return OTA_MANAGER_DEPLOYMENT_MISSING_FIELD;
    }
    if (context->kind != OTA_MANAGER_DEPLOYMENT_KIND_INSTALL &&
        context->kind != OTA_MANAGER_DEPLOYMENT_KIND_ROLLBACK) {
        return OTA_MANAGER_DEPLOYMENT_CONTRADICTORY;
    }
    if (!context->update_available) {
        return OTA_MANAGER_DEPLOYMENT_CONTRADICTORY;
    }
    if (strcmp(context->target_version, release_version) != 0) {
        return OTA_MANAGER_DEPLOYMENT_CONTRADICTORY;
    }
    if (context->kind == OTA_MANAGER_DEPLOYMENT_KIND_INSTALL) {
        if (ota_manager_string_is_nonempty(context->rollback_release_id) ||
            ota_manager_string_is_nonempty(
                context->rollback_of_deployment_id
            )) {
            return OTA_MANAGER_DEPLOYMENT_CONTRADICTORY;
        }
        return OTA_MANAGER_DEPLOYMENT_OK;
    }
    if (!context->rollback_available ||
        !ota_manager_is_lower_snake_identifier(
            context->rollback_release_id,
            OTA_MANAGER_RELEASE_ID_SIZE - 1
        ) ||
        !ota_manager_is_lower_snake_identifier(
            context->rollback_of_deployment_id,
            OTA_MANAGER_DEPLOYMENT_ID_SIZE - 1
        )) {
        return OTA_MANAGER_DEPLOYMENT_MISSING_FIELD;
    }
    if (strcmp(context->rollback_release_id, release_id) != 0) {
        return OTA_MANAGER_DEPLOYMENT_CONTRADICTORY;
    }
    if (!release_rollback_allowed) {
        return OTA_MANAGER_DEPLOYMENT_ROLLBACK_NOT_ALLOWED;
    }
    return OTA_MANAGER_DEPLOYMENT_OK;
}

static inline const char *ota_manager_deployment_error_name(
    ota_manager_deployment_error_t error
) {
    switch (error) {
        case OTA_MANAGER_DEPLOYMENT_OK:
            return "ok";
        case OTA_MANAGER_DEPLOYMENT_MISSING_FIELD:
            return "missing_deployment_field";
        case OTA_MANAGER_DEPLOYMENT_CONTRADICTORY:
            return "contradictory_deployment";
        case OTA_MANAGER_DEPLOYMENT_ROLLBACK_NOT_ALLOWED:
            return "rollback_not_allowed";
        default:
            return "unknown_deployment_error";
    }
}

#ifdef __cplusplus
}
#endif
