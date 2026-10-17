#pragma once

#include "esp_err.h"

#define TRANSPORT_SECURITY_OK 0

typedef struct {
    int reserved;
} transport_security_endpoint_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
);

#ifdef __cplusplus
}
#endif
