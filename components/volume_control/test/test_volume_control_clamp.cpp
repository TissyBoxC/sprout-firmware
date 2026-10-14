#include "volume_control.h"

#include <cassert>

// A value persisted under a looser policy cap must not survive a stricter
// guardian limit, so init clamps it back to the loaded maximum. This needs its
// own process because volume_control_init is idempotent once ready.
void volume_test_store_reset(bool ready);
void volume_test_store_put(const char *key, const char *value);

int main() {
    volume_test_store_reset(true);
    volume_test_store_put("audio_max_vol", "50");
    volume_test_store_put("audio_volume", "90");

    assert(volume_control_init() == ESP_OK);
    assert(volume_control_get_max_percent() == 50u);
    assert(volume_control_get_percent() == 50u);

    return 0;
}
