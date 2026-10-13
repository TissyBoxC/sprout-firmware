#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_capabilities.h"
#include "form_factor_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Map the compiled device capabilities to form-factor capability bits.
 *
 * The form-factor contract has its own, smaller capability vocabulary that the
 * platform matches against the composed body profile. The mapping is the single
 * place the two contracts are related, so a capability added to one is not
 * silently dropped by the other.
 */
uint32_t form_factor_report_capabilities(device_capability_bitmap_t capability);

/**
 * @brief Sanitize one device profile into a reportable form-factor payload.
 *
 * The device name is copied only when it fits the platform bound; an empty or
 * over-long name falls back to the profile default. The capability mask is the
 * intersection of the composed body profile and the capabilities this firmware
 * actually compiles, so a device can never advertise hardware it does not have.
 * The action mask is always recomposed from the resulting capabilities.
 *
 * @param[in] profile The local profile, or NULL for the default.
 * @param[in] compiled Compiled device capabilities to constrain the report.
 * @param[out] out Receives the sanitized profile. Must not be NULL.
 */
void form_factor_report_build(
    const form_factor_profile_t *profile,
    device_capability_bitmap_t compiled,
    form_factor_profile_t *out
);

#ifdef __cplusplus
}
#endif
