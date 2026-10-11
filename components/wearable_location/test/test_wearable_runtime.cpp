#include "wearable_location.h"

#include <cassert>

int main() {
    assert(wearable_init() == ESP_OK);
    assert(wearable_is_ready());
    assert(wearable_sos_state() == SOS_STATE_IDLE);
    assert(!wearable_sos_active());

    const location_policy_t defaults = wearable_get();
    assert(defaults.permission_granted == 0u);

    // Without a permission neither sampling nor an SOS is allowed.
    assert(!location_can_sample(&defaults));
    assert(wearable_sos_arm() == ESP_ERR_INVALID_STATE);
    assert(wearable_sos_trigger() == ESP_ERR_INVALID_STATE);

    // Granting the permission and enabling location makes the wearable usable.
    location_policy_t policy = defaults;
    policy.enabled = 1u;
    policy.permission_granted = 1u;
    assert(wearable_set(&policy) == ESP_OK);
    const location_policy_t applied = wearable_get();
    assert(location_can_sample(&applied));
    assert(wearable_sos_arm() == ESP_OK);
    assert(wearable_sos_state() == SOS_STATE_ARMED);

    // Triggering latches the SOS until it is cancelled.
    assert(wearable_sos_trigger() == ESP_OK);
    assert(wearable_sos_state() == SOS_STATE_TRIGGERED);
    assert(wearable_sos_active());
    // A second trigger stays triggered rather than clearing the emergency.
    assert(wearable_sos_trigger() == ESP_OK);
    assert(wearable_sos_active());

    assert(wearable_sos_cancel() == ESP_OK);
    assert(wearable_sos_state() == SOS_STATE_CANCELLED);
    assert(!wearable_sos_active());
    // Cancelling twice is rejected.
    assert(wearable_sos_cancel() == ESP_ERR_INVALID_STATE);

    // Revoking the permission clears an active SOS and stops sampling.
    assert(wearable_sos_trigger() == ESP_OK);
    assert(wearable_revoke_permission() == ESP_OK);
    const location_policy_t revoked = wearable_get();
    assert(!location_can_sample(&revoked));
    assert(wearable_sos_state() == SOS_STATE_IDLE);
    assert(!wearable_sos_active());

    // Persistence round trip keeps the edited policy.
    location_policy_t persisted = wearable_get();
    persisted.enabled = 0u;
    persisted.retention_days = 30u;
    assert(wearable_set(&persisted) == ESP_OK);
    assert(wearable_init() == ESP_OK);
    assert(wearable_get().enabled == 0u);
    assert(wearable_get().retention_days == 30u);

    // A policy that is not version-compatible is refused by apply.
    location_policy_t invalid;
    invalid.schema_version = 42u;
    assert(wearable_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(wearable_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor =
        wearable_location_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
