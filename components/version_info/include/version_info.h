#pragma once

#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Immutable versions reported by device health payloads. */
typedef struct {
    const char *schema_version;
    const char *firmware_version;
    const char *hardware_revision;
    const char *protocol_version;
} version_info_t;

/** @brief Return a snapshot of compile-time and application version data. */
version_info_t version_info_get(void);

/** @brief Return the removable-module descriptor for version_info. */
const module_descriptor_t *version_info_module_descriptor(void);

#ifdef __cplusplus
}
#endif
