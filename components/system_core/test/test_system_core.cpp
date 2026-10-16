#include "system_core.h"

#include <cassert>
#include <cstring>

void system_core_host_set_create_result(esp_err_t result);
int system_core_host_create_calls(void);

namespace {

void test_init_creates_the_loop_once() {
    system_core_host_set_create_result(ESP_OK);
    const int before = system_core_host_create_calls();

    assert(system_core_init() == ESP_OK);
    assert(system_core_host_create_calls() == before + 1);
    // The loop handle is valid after a successful init.
    assert(system_core_event_loop() != nullptr);

    // Idempotent: a second init must reuse the existing loop.
    assert(system_core_init() == ESP_OK);
    assert(system_core_host_create_calls() == before + 1);
    assert(system_core_event_loop() != nullptr);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor = system_core_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "system_core") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_init_creates_the_loop_once();
    test_descriptor_is_stable();
    return 0;
}
