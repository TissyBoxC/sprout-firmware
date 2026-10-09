#pragma once

#include "transport_security.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Protocol tokens shared with the platform contract. */
#define TRANSPORT_SECURITY_SCHEME_HTTPS "https://"
#define TRANSPORT_SECURITY_SCHEME_WSS "wss://"
#define TRANSPORT_SECURITY_SCHEME_MQTTS "mqtts://"

/**
 * @brief Pure URL validation used by the runtime and host tests.
 *
 * This function has no ESP-IDF dependency. It intentionally does not perform
 * DNS resolution or network access; the caller must still attach the verified
 * certificate bundle to every TLS client.
 */
transport_security_error_t transport_security_core_validate_url(
    const char *url,
    const transport_security_policy_t *policy,
    transport_security_endpoint_t *endpoint_out
);

#ifdef __cplusplus
}
#endif
