#include "form_factor_profile.h"

#include <cassert>
#include <cstring>

int main() {
    assert(form_factor_init() == ESP_OK);
    assert(form_factor_is_ready());

    const form_factor_profile_t defaults = form_factor_get();
    assert(std::strcmp(defaults.technical_name, "desktop") == 0);
    assert(!form_factor_has_action(&defaults, FORM_FACTOR_ACTION_WALK));

    // Store a moving body; the action set is recomposed from the capability set.
    form_factor_profile_t moving;
    std::memset(&moving, 0, sizeof(moving));
    moving.schema_version = FORM_FACTOR_SCHEMA_VERSION;
    std::strcpy(moving.technical_name, "moving_body");
    moving.capability_mask =
        FORM_FACTOR_CAP_DISPLAY |
        FORM_FACTOR_CAP_MICROPHONE |
        FORM_FACTOR_CAP_SPEAKER |
        FORM_FACTOR_CAP_LOCOMOTION |
        FORM_FACTOR_CAP_SERVO;
    assert(form_factor_set(&moving) == ESP_OK);
    const form_factor_profile_t stored = form_factor_get();
    assert(std::strcmp(stored.technical_name, "moving_body") == 0);
    assert(form_factor_has_action(&stored, FORM_FACTOR_ACTION_WALK));
    assert(form_factor_has_action(&stored, FORM_FACTOR_ACTION_TAIL));

    // Persistence round trip keeps the composed profile.
    assert(form_factor_init() == ESP_OK);
    const form_factor_profile_t reloaded = form_factor_get();
    assert(std::strcmp(reloaded.technical_name, "moving_body") == 0);
    assert(form_factor_has_action(&reloaded, FORM_FACTOR_ACTION_WALK));

    // A profile with the wrong schema version is refused by apply, and a
    // profile whose actions do not match its capabilities is refused too.
    form_factor_profile_t invalid = defaults;
    invalid.schema_version = 42u;
    assert(form_factor_apply(&invalid) == ESP_ERR_INVALID_ARG);
    form_factor_profile_t inconsistent = defaults;
    inconsistent.action_mask |= FORM_FACTOR_ACTION_WALK;
    assert(form_factor_apply(&inconsistent) == ESP_ERR_INVALID_ARG);
    assert(form_factor_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = form_factor_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
