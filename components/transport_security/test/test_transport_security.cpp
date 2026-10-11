// Host test for the pure transport security policy.

#include "transport_security.h"

#include <cassert>
#include <cstring>

static void test_https_validation(void) {
    transport_security_endpoint_t endpoint = {};
    assert(transport_security_validate_url(
        "https://api.clarkhub.cn/v1/device",
        &endpoint
    ) == TRANSPORT_SECURITY_OK);
    assert(endpoint.protocol == TRANSPORT_SECURITY_PROTOCOL_HTTPS);
    assert(std::strcmp(endpoint.host, "api.clarkhub.cn") == 0);
    assert(endpoint.port == 443);
    assert(std::strcmp(endpoint.path, "/v1/device") == 0);
}

static void test_wss_and_mqtts_validation(void) {
    transport_security_endpoint_t endpoint = {};
    assert(transport_security_validate_url(
        "wss://voice.clarkhub.cn:443/v1/voice",
        &endpoint
    ) == TRANSPORT_SECURITY_OK);
    assert(endpoint.protocol == TRANSPORT_SECURITY_PROTOCOL_WSS);
    assert(std::strcmp(endpoint.host, "voice.clarkhub.cn") == 0);
    assert(endpoint.port == 443);

    assert(transport_security_validate_url(
        "mqtts://mqtt.clarkhub.cn:8883",
        &endpoint
    ) == TRANSPORT_SECURITY_OK);
    assert(endpoint.protocol == TRANSPORT_SECURITY_PROTOCOL_MQTTS);
    assert(endpoint.port == 8883);
    assert(std::strcmp(endpoint.path, "/") == 0);
}

static void test_cleartext_rejected(void) {
    transport_security_endpoint_t endpoint = {};
    assert(transport_security_validate_url(
        "http://api.clarkhub.cn",
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_CLEARTEXT);
    assert(transport_security_validate_url(
        "ws://voice.clarkhub.cn/v1/voice",
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_CLEARTEXT);
    assert(transport_security_validate_url(
        "mqtt://mqtt.clarkhub.cn",
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_CLEARTEXT);
}

static void test_malformed_urls_rejected(void) {
    transport_security_endpoint_t endpoint = {};
    assert(transport_security_validate_url(nullptr, &endpoint) ==
           TRANSPORT_SECURITY_ERR_NULL_ARGUMENT);
    assert(transport_security_validate_url("", &endpoint) ==
           TRANSPORT_SECURITY_ERR_EMPTY_URL);
    assert(transport_security_validate_url("ftp://api.clarkhub.cn", &endpoint) ==
           TRANSPORT_SECURITY_ERR_UNSUPPORTED_SCHEME);
    assert(transport_security_validate_url("https://", &endpoint) ==
           TRANSPORT_SECURITY_ERR_MISSING_HOST);
    assert(transport_security_validate_url("https://api.clarkhub.cn:0", &endpoint) ==
           TRANSPORT_SECURITY_ERR_INVALID_PORT);
    assert(transport_security_validate_url("https://api.clarkhub.cn:99999", &endpoint) ==
           TRANSPORT_SECURITY_ERR_INVALID_PORT);
    assert(transport_security_validate_url("https://api.clarkhub.cn:abc", &endpoint) ==
           TRANSPORT_SECURITY_ERR_INVALID_PORT);
    assert(transport_security_validate_url("https://api.clarkhub.cn/#fragment", &endpoint) ==
           TRANSPORT_SECURITY_ERR_FRAGMENT_NOT_ALLOWED);
    assert(transport_security_validate_url("https://user:pass@api.clarkhub.cn", &endpoint) ==
           TRANSPORT_SECURITY_ERR_EMBEDDED_CREDENTIALS);
    assert(transport_security_validate_url("https://bad host/", &endpoint) ==
           TRANSPORT_SECURITY_ERR_INVALID_HOST);
}

static void test_policy_cannot_be_weakened(void) {
    transport_security_endpoint_t endpoint = {};
    transport_security_policy_t policy =
        transport_security_production_policy();
    policy.verify_peer = false;
    assert(transport_security_validate_url_with_policy(
        "https://api.clarkhub.cn",
        &policy,
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_CERTIFICATE_DISABLED);

    policy = transport_security_production_policy();
    policy.allow_plaintext = true;
    assert(transport_security_validate_url_with_policy(
        "https://api.clarkhub.cn",
        &policy,
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_CLEARTEXT);

    policy = transport_security_production_policy();
    policy.allow_downgrade = true;
    assert(transport_security_validate_url_with_policy(
        "https://api.clarkhub.cn",
        &policy,
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE);
}

static void test_no_downgrade(void) {
    assert(transport_security_require_no_downgrade(
        TRANSPORT_SECURITY_PROTOCOL_WSS,
        TRANSPORT_SECURITY_PROTOCOL_WSS
    ) == TRANSPORT_SECURITY_OK);
    assert(transport_security_require_no_downgrade(
        TRANSPORT_SECURITY_PROTOCOL_WSS,
        TRANSPORT_SECURITY_PROTOCOL_HTTPS
    ) == TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE);
    assert(transport_security_require_no_downgrade(
        TRANSPORT_SECURITY_PROTOCOL_UNKNOWN,
        TRANSPORT_SECURITY_PROTOCOL_WSS
    ) == TRANSPORT_SECURITY_ERR_UNSUPPORTED_SCHEME);
}

static void test_protocol_names(void) {
    assert(std::strcmp(
        transport_security_protocol_name(
            TRANSPORT_SECURITY_PROTOCOL_MQTTS
        ),
        "mqtts"
    ) == 0);
    assert(std::strcmp(
        transport_security_error_name(
            TRANSPORT_SECURITY_ERR_CLEARTEXT
        ),
        "cleartext_rejected"
    ) == 0);
}

static void test_allowed_host_suffixes(void) {
    assert(transport_security_init() == ESP_OK);
    // The base suffix matches its apex and any subdomain.
    assert(transport_security_host_is_allowed("clarkhub.cn"));
    assert(transport_security_host_is_allowed("api.clarkhub.cn"));
    assert(transport_security_host_is_allowed("voice.clarkhub.cn"));
    // Case-insensitive matching.
    assert(transport_security_host_is_allowed("API.ClarkHub.CN"));
    // A coincidental substring is not the same registrable domain.
    assert(!transport_security_host_is_allowed("evilclarkhub.cn"));
    assert(!transport_security_host_is_allowed("clarkhub.cn.evil.com"));
    // The compiled extra suffix is trusted, and only as a real domain.
    assert(transport_security_host_is_allowed("example.com"));
    assert(transport_security_host_is_allowed("cdn.example.com"));
    assert(!transport_security_host_is_allowed("notexample.com"));
    // An unlisted domain stays rejected.
    assert(!transport_security_host_is_allowed("exfil.example.net"));
    assert(!transport_security_host_is_allowed(nullptr));
    assert(!transport_security_host_is_allowed(""));

    // The outbound helper combines the policy with the allowlist.
    transport_security_endpoint_t endpoint = {};
    assert(transport_security_validate_outbound(
        "https://api.clarkhub.cn/v1/device",
        &endpoint
    ) == TRANSPORT_SECURITY_OK);
    assert(std::strcmp(endpoint.host, "api.clarkhub.cn") == 0);
    // A secure scheme alone is not enough; the host must be trusted.
    assert(transport_security_validate_outbound(
        "https://exfil.example.net/steal",
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_INVALID_HOST);
    // Cleartext is still rejected before the allowlist is consulted.
    assert(transport_security_validate_outbound(
        "http://api.clarkhub.cn",
        &endpoint
    ) == TRANSPORT_SECURITY_ERR_CLEARTEXT);
}

int main(void) {
    test_https_validation();
    test_wss_and_mqtts_validation();
    test_cleartext_rejected();
    test_malformed_urls_rejected();
    test_policy_cannot_be_weakened();
    test_no_downgrade();
    test_protocol_names();
    test_allowed_host_suffixes();
    return 0;
}
