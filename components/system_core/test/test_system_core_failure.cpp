#include "system_core.h"

#include <cassert>

void system_core_host_set_create_result(esp_err_t result);

// The loop creation failure must be propagated and must not leave a usable
// handle behind. This needs its own process because system_core_init is
// idempotent once a loop exists.
int main() {
    system_core_host_set_create_result(ESP_ERR_NO_MEM);

    assert(system_core_init() == ESP_ERR_NO_MEM);
    assert(system_core_event_loop() == nullptr);

    // A later successful attempt may then create the loop.
    system_core_host_set_create_result(ESP_OK);
    assert(system_core_init() == ESP_OK);
    assert(system_core_event_loop() != nullptr);
    return 0;
}
