#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_library.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of theme packs cached on the device. */
#define THEME_PACKAGE_CAPACITY 8

/** @brief Maximum number of member packages per theme pack. */
#define THEME_PACKAGE_MEMBER_CAPACITY 16

/** @brief Maximum theme title length including the terminator. */
#define THEME_PACKAGE_TITLE_SIZE 64

/** @brief One cached theme pack: an ordered collection of content packages. */
typedef struct {
    char theme_id[CONTENT_LIBRARY_ID_SIZE];
    char title[THEME_PACKAGE_TITLE_SIZE];
    char package_ids[THEME_PACKAGE_MEMBER_CAPACITY][CONTENT_LIBRARY_ID_SIZE];
    size_t member_count;
    int64_t catalog_revision;
} theme_pack_t;

/** @brief Stable theme-package error codes for diagnostics and UI copy. */
typedef enum {
    THEME_PACKAGE_OK = 0,
    THEME_PACKAGE_ERR_NOT_INITIALIZED,
    THEME_PACKAGE_ERR_INVALID_ARGUMENT,
    THEME_PACKAGE_ERR_FULL,
    THEME_PACKAGE_ERR_NOT_FOUND,
    THEME_PACKAGE_ERR_STORAGE,
} theme_package_error_t;

/** @brief Initialize the theme cache from the configuration store. */
esp_err_t theme_package_init(void);

/** @brief Return true when the theme cache is ready. */
bool theme_package_is_ready(void);

/** @brief Insert or replace one theme pack, newest change first. */
esp_err_t theme_package_upsert(const theme_pack_t *pack);

/** @brief Remove one theme pack. A missing pack is success. */
esp_err_t theme_package_remove(const char *theme_id);

/** @brief Copy the cached theme packs newest-first. */
esp_err_t theme_package_list(
    theme_pack_t *packs_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return one cached theme pack by id. */
bool theme_package_get(const char *theme_id, theme_pack_t *pack_out);

/**
 * @brief Check whether one package id belongs to a cached theme pack.
 *
 * When member_index_out is provided it receives the member position.
 */
bool theme_package_contains_package(
    const char *theme_id,
    const char *package_id,
    size_t *member_index_out
);

/** @brief Return the current number of cached theme packs. */
size_t theme_package_count(void);

/** @brief Return the stable string for one theme-package error code. */
const char *theme_package_error_name(theme_package_error_t error);

/** @brief Return the removable-module descriptor for theme_package. */
const module_descriptor_t *theme_package_module_descriptor(void);

#ifdef __cplusplus
}
#endif
