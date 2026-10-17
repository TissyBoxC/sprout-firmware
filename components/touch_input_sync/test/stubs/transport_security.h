#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRANSPORT_SECURITY_OK = 0,
    TRANSPORT_SECURITY_ERR_CLEARTEXT,
} transport_security_error_t;

typedef struct {
    int reserved;
} transport_security_endpoint_t;

transport_security_error_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
);

#ifdef __cplusplus
}
#endif
