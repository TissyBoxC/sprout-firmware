#pragma once

// Minimal host stub so the pure download state machine and playback chunker
// can be compiled without ESP-IDF. Only the symbols referenced by the public
// headers exist.

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
