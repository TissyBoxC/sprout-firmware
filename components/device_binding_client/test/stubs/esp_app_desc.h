#pragma once

#include <stdint.h>

typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    char version[32];
    char project_name[32];
    char idf_ver[32];
} esp_app_desc_t;

#ifdef __cplusplus
extern "C" {
#endif

const esp_app_desc_t *esp_app_get_description(void);

#ifdef __cplusplus
}
#endif
