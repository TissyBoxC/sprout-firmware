#include "module_registry.h"

#include <cassert>

// Capacity exhaustion needs a tiny static capacity, so it runs in its own
// process built with -DCONFIG_MODULE_REGISTRY_CAPACITY=2.
namespace {

esp_err_t noop_init(void) {
    return ESP_OK;
}

const module_descriptor_t g_first = {
    "first", "1.0.0", noop_init, nullptr,
};
const module_descriptor_t g_second = {
    "second", "1.0.0", noop_init, nullptr,
};
const module_descriptor_t g_overflow = {
    "overflow", "1.0.0", noop_init, nullptr,
};

}  // namespace

int main() {
    assert(module_registry_add(&g_first) == ESP_OK);
    assert(module_registry_add(&g_second) == ESP_OK);
    // The static array is full, so a further registration cannot be stored.
    assert(module_registry_add(&g_overflow) == ESP_ERR_NO_MEM);
    assert(module_registry_initialized_count() == 0u);
    return 0;
}
