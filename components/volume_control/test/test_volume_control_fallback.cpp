#include "volume_control.h"

#include <cassert>

// An out-of-range persisted value is not trusted; init keeps the compiled
// default, bounded by the configured maximum. Needs its own process because
// volume_control_init is idempotent once ready.
void volume_test_store_reset(bool ready);
void volume_test_store_put(const char *key, const char *value);

int main() {
    volume_test_store_reset(true);
    volume_test_store_put("audio_max_vol", "70");
    volume_test_store_put("audio_volume", "200");

    assert(volume_control_init() == ESP_OK);
    assert(volume_control_get_max_percent() == 70u);
    // 200 is out of range, so the default 40 is used instead.
    assert(volume_control_get_percent() == 40u);

    return 0;
}
