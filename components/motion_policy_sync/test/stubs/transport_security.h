#pragma once

#include <stdint.h>

typedef enum {
    TRANSPORT_SECURITY_OK = 0,
    TRANSPORT_SECURITY_ERR_CLEARTEXT,
    TRANSPORT_SECURITY_ERR_HOST_NOT_ALLOWED,
    TRANSPORT_SECURITY_ERR_INVALID_ARG,
} transport_security_error_t;

typedef struct {
    const char *scheme;
    const char *host;
    uint16_t port;
} transport_security_endpoint_t;

#ifdef __cplusplus
extern "C" {
#endif

transport_security_error_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
);

#ifdef __cplusplus
}
#endif
