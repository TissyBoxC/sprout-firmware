#include "touch_input.h"

#include <cassert>

int main() {
    assert(touch_input_init() == ESP_OK);
    assert(touch_input_is_ready());

    const touch_input_config_t defaults = touch_input_get();
    assert(defaults.sensitivity == 60u);

    touch_input_config_t requested = defaults;
    requested.sensitivity = 200u;
    requested.long_press_ms = 9999u;
    assert(touch_input_set(&requested) == ESP_OK);
    const touch_input_config_t stored = touch_input_get();
    assert(stored.sensitivity == TOUCH_INPUT_MAX_SENSITIVITY);
    assert(stored.long_press_ms == TOUCH_INPUT_MAX_LONG_PRESS_MS);

    touch_input_config_t persisted = stored;
    persisted.haptic_enabled = 0u;
    assert(touch_input_set(&persisted) == ESP_OK);
    assert(touch_input_init() == ESP_OK);
    assert(touch_input_get().haptic_enabled == 0u);

    touch_input_config_t invalid;
    invalid.schema_version = 42u;
    assert(touch_input_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(touch_input_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor =
        touch_input_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
