#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVICE_MESSAGE_OK = 0,
    DEVICE_MESSAGE_ERR_NOT_INITIALIZED,
    DEVICE_MESSAGE_ERR_INVALID_ARG,
    DEVICE_MESSAGE_ERR_INVALID_PAYLOAD,
    DEVICE_MESSAGE_ERR_NO_DISPLAY,
    DEVICE_MESSAGE_ERR_BUSY,
} device_message_error_t;

typedef struct {
    char id[64];
    char title[128];
    char body[512];
    char severity[16];
    char category[32];
    uint32_t duration_seconds;
} device_message_t;

esp_err_t device_message_present(
    const device_message_t *message,
    device_message_error_t *error_out
);

#ifdef __cplusplus
}
#endif
