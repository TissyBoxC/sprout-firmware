#include "esp_system.h"
#include "freertos/task.h"
#include "module_registry.h"

// The host test deliberately never enters error_recovery_restart, so these
// definitions only satisfy the linker. module_registry is not compiled into
// this test; the registry handler registration is captured as a no-op.
static module_failure_handler_t host_failure_handler;
static module_recovery_handler_t host_recovery_handler;

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

void esp_restart(void) {
}

void module_registry_set_failure_handler(module_failure_handler_t handler) {
    host_failure_handler = handler;
}

void module_registry_set_recovery_handler(module_recovery_handler_t handler) {
    host_recovery_handler = handler;
}

// Keep the captured handlers referenced so a future test can assert that the
// descriptor wired the recovery module as the registry observer.
module_failure_handler_t error_recovery_host_failure_handler(void) {
    return host_failure_handler;
}

module_recovery_handler_t error_recovery_host_recovery_handler(void) {
    return host_recovery_handler;
}
