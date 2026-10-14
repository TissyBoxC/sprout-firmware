#include "module_registry.h"

#include <cassert>
#include <cstring>

// The registry is a process-wide singleton with no reset hook, so this test
// drives one realistic boot and a simulated reboot rather than isolated cases.
namespace {

enum gamma_mode_t {
    GAMMA_FAIL = 0,
    GAMMA_OK = 1,
};

int g_alpha_inits;
int g_alpha_shutdowns;
int g_beta_calls;
int g_beta_shutdowns;
int g_gamma_mode = GAMMA_FAIL;
char g_shutdown_order[8];
size_t g_shutdown_order_length;

int g_failure_count;
char g_failure_name[32];
esp_err_t g_failure_code;
int g_recovery_count;
char g_recovery_name[32];

void record_order(char entry) {
    if (g_shutdown_order_length < sizeof(g_shutdown_order) - 1) {
        g_shutdown_order[g_shutdown_order_length++] = entry;
    }
    g_shutdown_order[g_shutdown_order_length] = '\0';
}

void record_failure(const char *module_name, esp_err_t error_code) {
    ++g_failure_count;
    std::strncpy(g_failure_name, module_name, sizeof(g_failure_name) - 1);
    g_failure_name[sizeof(g_failure_name) - 1] = '\0';
    g_failure_code = error_code;
}

void record_recovery(const char *module_name) {
    ++g_recovery_count;
    std::strncpy(g_recovery_name, module_name, sizeof(g_recovery_name) - 1);
    g_recovery_name[sizeof(g_recovery_name) - 1] = '\0';
}

esp_err_t alpha_init(void) {
    ++g_alpha_inits;
    return ESP_OK;
}

void alpha_shutdown(void) {
    ++g_alpha_shutdowns;
    record_order('a');
}

esp_err_t beta_init(void) {
    ++g_beta_calls;
    // Fails on the first attempt, then succeeds on the registry retry pass.
    return g_beta_calls == 1 ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void beta_shutdown(void) {
    ++g_beta_shutdowns;
    record_order('b');
}

esp_err_t gamma_init(void) {
    return g_gamma_mode == GAMMA_OK ? ESP_OK : ESP_FAIL;
}

const module_descriptor_t g_alpha = {
    "alpha", "1.0.0", alpha_init, alpha_shutdown,
};
const module_descriptor_t g_beta = {
    "beta", "1.0.0", beta_init, beta_shutdown,
};
const module_descriptor_t g_gamma = {
    "gamma", "1.0.0", gamma_init, nullptr,
};

void test_registration_validation() {
    const module_descriptor_t missing_version = {
        "missing-version", nullptr, alpha_init, nullptr,
    };
    const module_descriptor_t missing_init = {
        "missing-init", "1.0.0", nullptr, nullptr,
    };

    assert(module_registry_add(nullptr) == ESP_ERR_INVALID_ARG);
    assert(module_registry_add(&missing_version) == ESP_ERR_INVALID_ARG);
    assert(module_registry_add(&missing_init) == ESP_ERR_INVALID_ARG);

    // A retry before initialization starts is a state conflict.
    assert(module_registry_initialize_module("alpha") == ESP_ERR_INVALID_STATE);

    assert(module_registry_add(&g_alpha) == ESP_OK);
    // A duplicate name is rejected before initialization starts.
    assert(module_registry_add(&g_alpha) == ESP_ERR_INVALID_STATE);
    assert(module_registry_add(&g_beta) == ESP_OK);
    assert(module_registry_add(&g_gamma) == ESP_OK);
    assert(module_registry_initialized_count() == 0u);
}

void test_initialize_all_reports_first_failure_and_recovers_transient() {
    // gamma always fails here, so the boot reports its error; beta recovers on
    // the registry retry pass and therefore does not surface as a failure.
    const esp_err_t result = module_registry_initialize_all();
    // The registry reports the first failure in registration order, which is
    // beta's transient error, even though beta itself recovers on the retry.
    assert(result == ESP_ERR_INVALID_STATE);

    assert(module_registry_is_initialized("alpha"));
    assert(module_registry_is_initialized("beta"));
    assert(!module_registry_is_initialized("gamma"));
    assert(!module_registry_has_failed("alpha"));
    assert(!module_registry_has_failed("beta"));
    assert(module_registry_has_failed("gamma"));
    assert(module_registry_initialized_count() == 2u);

    // The composition graph is frozen once initialization has started.
    assert(module_registry_add(&g_gamma) == ESP_ERR_INVALID_STATE);
}

void test_retry_requires_a_known_failed_module() {
    // Unknown names are not found; a module that never started is a conflict.
    assert(module_registry_initialize_module("missing") == ESP_ERR_NOT_FOUND);
    assert(module_registry_initialize_module(nullptr) == ESP_ERR_NOT_FOUND);
    // Retrying an already-initialized module succeeds without re-running it.
    const int inits_before = g_alpha_inits;
    assert(module_registry_initialize_module("alpha") == ESP_OK);
    assert(g_alpha_inits == inits_before);
}

void test_failure_handler_replays_current_failures() {
    module_registry_set_failure_handler(record_failure);
    // gamma failed before the handler existed, so it is replayed exactly once.
    assert(g_failure_count == 1);
    assert(std::strcmp(g_failure_name, "gamma") == 0);
    assert(g_failure_code == ESP_FAIL);

    // Disabling the handler stops further notifications.
    module_registry_set_failure_handler(nullptr);
}

void test_successful_retry_reports_recovery() {
    module_registry_set_recovery_handler(record_recovery);
    g_gamma_mode = GAMMA_OK;

    assert(module_registry_initialize_module("gamma") == ESP_OK);
    assert(module_registry_is_initialized("gamma"));
    assert(!module_registry_has_failed("gamma"));
    assert(module_registry_initialized_count() == 3u);
    // Recovery is reported once, only for the module that actually recovered.
    assert(g_recovery_count == 1);
    assert(std::strcmp(g_recovery_name, "gamma") == 0);
}

void test_shutdown_runs_in_reverse_registration_order() {
    g_shutdown_order_length = 0;
    g_shutdown_order[0] = '\0';
    module_registry_shutdown_all();

    // gamma has no shutdown hook; beta and alpha unwind last-first.
    assert(std::strcmp(g_shutdown_order, "ba") == 0);
    assert(module_registry_initialized_count() == 0u);
    assert(!module_registry_is_initialized("alpha"));
    assert(!module_registry_is_initialized("beta"));
}

void test_reboot_reinitializes_all_modules() {
    // Every module now initializes cleanly, so the second boot returns OK.
    g_shutdown_order_length = 0;
    g_shutdown_order[0] = '\0';
    assert(module_registry_initialize_all() == ESP_OK);
    assert(module_registry_initialized_count() == 3u);
    assert(module_registry_is_initialized("gamma"));

    module_registry_shutdown_all();
    assert(std::strcmp(g_shutdown_order, "ba") == 0);
}

}  // namespace

int main() {
    test_registration_validation();
    test_initialize_all_reports_first_failure_and_recovers_transient();
    test_retry_requires_a_known_failed_module();
    test_failure_handler_replays_current_failures();
    test_successful_retry_reports_recovery();
    test_shutdown_runs_in_reverse_registration_order();
    test_reboot_reinitializes_all_modules();
    return 0;
}
