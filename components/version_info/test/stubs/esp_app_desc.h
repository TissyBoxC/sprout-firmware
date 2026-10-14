#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal host stand-in for the ESP-IDF application descriptor. Only the
// fields version_info reads are modeled, and a test hook mutates the version
// so both the healthy and empty-version paths can be exercised. The single
// definition lives in esp_app_desc.c so the setter and the reader share it.
typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    char version[32];
    char project_name[32];
    char idf_ver[32];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);
void version_info_test_set_firmware_version(const char *version);
