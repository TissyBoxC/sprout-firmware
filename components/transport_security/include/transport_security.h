#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum accepted endpoint length including the terminator. */
#define TRANSPORT_SECURITY_URL_SIZE 512

/** @brief Maximum accepted host length including the terminator. */
#define TRANSPORT_SECURITY_HOST_SIZE 256

/** @brief Secure transports accepted by the device. */
typedef enum {
    TRANSPORT_SECURITY_PROTOCOL_UNKNOWN = 0,
    TRANSPORT_SECURITY_PROTOCOL_HTTPS,
    TRANSPORT_SECURITY_PROTOCOL_WSS,
    TRANSPORT_SECURITY_PROTOCOL_MQTTS,
} transport_security_protocol_t;

/** @brief Stable rejection reasons for diagnostics and the parent app. */
typedef enum {
    TRANSPORT_SECURITY_OK = 0,
    TRANSPORT_SECURITY_ERR_NULL_ARGUMENT,
    TRANSPORT_SECURITY_ERR_EMPTY_URL,
    TRANSPORT_SECURITY_ERR_UNSUPPORTED_SCHEME,
    TRANSPORT_SECURITY_ERR_CLEARTEXT,
    TRANSPORT_SECURITY_ERR_MISSING_HOST,
    TRANSPORT_SECURITY_ERR_INVALID_HOST,
    TRANSPORT_SECURITY_ERR_INVALID_PORT,
    TRANSPORT_SECURITY_ERR_EMBEDDED_CREDENTIALS,
    TRANSPORT_SECURITY_ERR_FRAGMENT_NOT_ALLOWED,
    TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE,
    TRANSPORT_SECURITY_ERR_CERTIFICATE_DISABLED,
} transport_security_error_t;

/** @brief Parsed and validated transport endpoint. */
typedef struct {
    transport_security_protocol_t protocol;
    char host[TRANSPORT_SECURITY_HOST_SIZE];
    uint16_t port;
    const char *path;
} transport_security_endpoint_t;

/** @brief Certificate policy applied to every outbound TLS connection. */
typedef struct {
    bool verify_peer;
    bool verify_hostname;
    bool allow_plaintext;
    bool allow_downgrade;
} transport_security_policy_t;

/**
 * @brief Return the production policy.
 *
 * Peer and hostname verification are always enabled, while cleartext
 * transports and protocol downgrades are always rejected. Callers use this
 * value instead of assembling their own policy so a future module cannot
 * weaken one connection independently.
 */
transport_security_policy_t transport_security_production_policy(void);

/**
 * @brief Parse and validate one endpoint against the production policy.
 *
 * Accepted schemes are `https`, `wss`, and `mqtts`. The function rejects
 * cleartext schemes, embedded credentials, fragments, malformed ports, and
 * hosts that are empty or contain unsafe characters. The returned path points
 * into the input string and remains valid for the lifetime of that string.
 */
transport_security_error_t transport_security_validate_url(
    const char *url,
    transport_security_endpoint_t *endpoint_out
);

/**
 * @brief Apply an explicit policy when the caller has a legitimate reason.
 *
 * Only the firmware composition root and host tests pass a custom policy.
 * Passing a policy that disables peer or hostname verification, enables
 * cleartext, or enables downgrades returns
 * TRANSPORT_SECURITY_ERR_CERTIFICATE_DISABLED or
 * TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE.
 */
transport_security_error_t transport_security_validate_url_with_policy(
    const char *url,
    const transport_security_policy_t *policy,
    transport_security_endpoint_t *endpoint_out
);

/**
 * @brief Require that a replacement endpoint stays at the same or higher level.
 *
 * Used when a server redirect, configuration refresh, or failover changes an
 * endpoint. A secure protocol may only be replaced by the same protocol; a
 * cleartext source is rejected outright so a downgrade can never be adopted.
 */
transport_security_error_t transport_security_require_no_downgrade(
    transport_security_protocol_t current,
    transport_security_protocol_t candidate
);

/** @brief Return the stable name for one protocol. */
const char *transport_security_protocol_name(
    transport_security_protocol_t protocol
);

/** @brief Return the stable text for one validation error. */
const char *transport_security_error_name(
    transport_security_error_t error
);

/** @brief Initialize the shared transport policy. */
esp_err_t transport_security_init(void);

/** @brief Return true after the shared policy initialized successfully. */
bool transport_security_is_ready(void);

/** @brief Return the active policy; production values before initialization. */
transport_security_policy_t transport_security_get_policy(void);

/** @brief Return the removable-module descriptor for transport_security. */
const module_descriptor_t *transport_security_module_descriptor(void);

#ifdef __cplusplus
}
#endif
