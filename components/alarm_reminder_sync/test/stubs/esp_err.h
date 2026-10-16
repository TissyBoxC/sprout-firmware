#pragma once

// Minimal host stub so the alarm reminder sync module can be compiled without
// ESP-IDF. Only the error codes referenced by the module and its real
// dependencies exist.

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107

#ifdef __cplusplus
extern "C" {
#endif

const char *esp_err_to_name(esp_err_t error);

#ifdef __cplusplus
}
#endif
