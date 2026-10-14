#include "volume_control.h"

#include <cassert>

// The host store stub exposes two helpers so this test can seed persisted
// values before the module reads them at init.
void volume_test_store_reset(bool ready);
void volume_test_store_put(const char *key, const char *value);

int main() {
    volume_test_store_reset(true);
    volume_test_store_put("audio_max_vol", "80");
    // Stored under a looser cap that the loaded policy still allows.
    volume_test_store_put("audio_volume", "75");
    volume_test_store_put("audio_muted", "1");

    assert(volume_control_init() == ESP_OK);
    assert(volume_control_get_max_percent() == 80u);
    assert(volume_control_is_muted());

    // The stored volume survives while muted, and is revealed on unmute.
    assert(volume_control_set_muted(false) == ESP_OK);
    assert(volume_control_get_percent() == 75u);

    // Lowering the cap reduces the live volume immediately.
    assert(volume_control_set_max_percent(50u) == VOLUME_CONTROL_OK);
    assert(volume_control_get_percent() == 50u);

    return 0;
}
