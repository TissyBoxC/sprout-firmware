#pragma once

#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "ota_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verify one written OTA partition before boot selection.
 *
 * The image header, application descriptor, version, and (when Secure Boot is
 * enabled) the Secure Boot signature are checked. A failure is final: the
 * caller must not select the partition for boot.
 */
esp_err_t ota_validate_partition(
    const esp_partition_t *partition,
    const ota_manifest_t *manifest
);

#ifdef __cplusplus
}
#endif
