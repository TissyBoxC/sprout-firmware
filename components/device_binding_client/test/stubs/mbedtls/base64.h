#pragma once

#include <stddef.h>

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL -0x002A

#ifdef __cplusplus
extern "C" {
#endif

int mbedtls_base64_encode(
    unsigned char *dst,
    size_t dlen,
    size_t *olen,
    const unsigned char *src,
    size_t slen
);

#ifdef __cplusplus
}
#endif
