#include "transport_security.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "transport_security_core.h"

static const char *const TAG = "transport_security";

static bool transport_security_ready;

// The suffix list is compiled from the base suffix plus any additional
// comma-separated suffixes. It is assembled once so a connection path does not
// rebuild it on every validation.
static char transport_security_allowed_suffixes[
    TRANSPORT_SECURITY_HOST_SIZE
];

static void transport_security_allowed_suffixes_init(void) {
    const char *configured = "";
#ifdef CONFIG_TRANSPORT_SECURITY_ALLOWED_HOST
    configured = CONFIG_TRANSPORT_SECURITY_ALLOWED_HOST;
#endif
    if (configured[0] == '\0') {
        // Keep only the base suffix when no additional host is configured.
        snprintf(
            transport_security_allowed_suffixes,
            sizeof(transport_security_allowed_suffixes),
            "%s",
            TRANSPORT_SECURITY_BASE_HOST_SUFFIX
        );
        return;
    }
    snprintf(
        transport_security_allowed_suffixes,
        sizeof(transport_security_allowed_suffixes),
        "%s,%s",
        TRANSPORT_SECURITY_BASE_HOST_SUFFIX,
        configured
    );
}

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

static bool transport_security_host_matches_suffix(
    const char *host,
    size_t host_length,
    const char *suffix
) {
    const size_t suffix_length = strlen(suffix);
    if (suffix_length == 0 || host_length < suffix_length) {
        return false;
    }
    size_t compare_length = suffix_length;
    if (suffix[0] == '.') {
        // A leading dot means an explicit subdomain-only match.
        compare_length = suffix_length - 1;
        if (host_length <= compare_length) {
            return false;
        }
    }
    const char *host_tail = host + host_length - compare_length;
    for (size_t index = 0; index < compare_length; ++index) {
        const unsigned char host_byte = (unsigned char)host_tail[index];
        const unsigned char suffix_byte =
            (unsigned char)suffix[suffix[0] == '.' ? index + 1 : index];
        if (tolower(host_byte) != tolower(suffix_byte)) {
            return false;
        }
    }
    if (suffix[0] == '.') {
        return true;
    }
    // A bare suffix matches the apex domain itself or a subdomain whose label
    // boundary is a dot, never a coincidental substring such as
    // "evilclarkhub.cn".
    if (host_length == compare_length) {
        return true;
    }
    return host[host_length - compare_length - 1] == '.';
}

bool transport_security_host_is_allowed(const char *host) {
    if (host == NULL || host[0] == '\0') {
        return false;
    }
    const size_t host_length = strlen(host);
    if (transport_security_allowed_suffixes[0] == '\0') {
        // A client may validate before transport_security_init runs; build the
        // suffix list on demand so a valid endpoint is never rejected purely
        // because of module ordering.
        transport_security_allowed_suffixes_init();
    }
    const char *cursor = transport_security_allowed_suffixes;
    while (*cursor != '\0') {
        const char *separator = strchr(cursor, ',');
        const size_t entry_length = separator == NULL
            ? strlen(cursor)
            : (size_t)(separator - cursor);
        if (entry_length > 0) {
            char entry[TRANSPORT_SECURITY_HOST_SIZE] = {0};
            const size_t copy_length = entry_length <
                    sizeof(entry) - 1
                ? entry_length
                : sizeof(entry) - 1;
            memcpy(entry, cursor, copy_length);
            entry[copy_length] = '\0';
            if (transport_security_host_matches_suffix(
                    host,
                    host_length,
                    entry
                )) {
                return true;
            }
        }
        if (separator == NULL) {
            break;
        }
        cursor = separator + 1;
    }
    return false;
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

transport_security_error_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint_out
) {
    transport_security_endpoint_t parsed = {};
    const transport_security_error_t result = transport_security_core_validate_url(
        url,
        &transport_security_active_policy,
        &parsed
    );
    if (result != TRANSPORT_SECURITY_OK) {
        return result;
    }
    if (!transport_security_host_is_allowed(parsed.host)) {
        return TRANSPORT_SECURITY_ERR_INVALID_HOST;
    }
    if (endpoint_out != NULL) {
        *endpoint_out = parsed;
    }
    return TRANSPORT_SECURITY_OK;
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
    transport_security_allowed_suffixes_init();
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
