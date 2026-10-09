#include "transport_security.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "transport_security_core.h"

static const char *const TAG = "transport_security";

static bool transport_security_ready;

static transport_security_policy_t transport_security_active_policy = {
    .verify_peer = true,
    .verify_hostname = true,
    .allow_plaintext = false,
    .allow_downgrade = false,
};

transport_security_policy_t transport_security_production_policy(void) {
    return (transport_security_policy_t){
        .verify_peer = true,
        .verify_hostname = true,
        .allow_plaintext = false,
        .allow_downgrade = false,
    };
}

transport_security_error_t transport_security_validate_url(
    const char *url,
    transport_security_endpoint_t *endpoint_out
) {
    return transport_security_core_validate_url(
        url,
        &transport_security_active_policy,
        endpoint_out
    );
}

transport_security_error_t transport_security_validate_url_with_policy(
    const char *url,
    const transport_security_policy_t *policy,
    transport_security_endpoint_t *endpoint_out
) {
    return transport_security_core_validate_url(url, policy, endpoint_out);
}

esp_err_t transport_security_init(void) {
    if (transport_security_ready) {
        return ESP_OK;
    }
    transport_security_active_policy =
        transport_security_production_policy();
    transport_security_ready = true;
    ESP_LOGI(TAG, "secure transport policy active");
    return ESP_OK;
}

bool transport_security_is_ready(void) {
    return transport_security_ready;
}

transport_security_policy_t transport_security_get_policy(void) {
    return transport_security_active_policy;
}

const module_descriptor_t *transport_security_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "transport_security",
        .version = "1.0.0",
        .initialize = transport_security_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
