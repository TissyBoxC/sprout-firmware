#include "transport_security_core.h"

#include <string.h>

static bool transport_security_character_is_control(char value) {
    const unsigned char byte = (unsigned char)value;
    return byte < 0x20 || byte == 0x7f;
}

static bool transport_security_character_is_forbidden(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
        value == '\\' || value == '#' || value == '?' || value == '@';
}

static bool transport_security_scheme_matches(
    const char *url,
    const char *scheme,
    transport_security_protocol_t protocol,
    transport_security_protocol_t *protocol_out
) {
    const size_t scheme_length = strlen(scheme);
    if (strncmp(url, scheme, scheme_length) != 0) {
        return false;
    }
    *protocol_out = protocol;
    return true;
}

static bool transport_security_host_is_valid(
    const char *host,
    size_t host_length
) {
    if (host_length == 0 || host_length >= TRANSPORT_SECURITY_HOST_SIZE) {
        return false;
    }
    // Bracketed IPv6 literals are accepted as-is; letters, digits, dots, and
    // hyphens cover DNS names and IPv4 literals.
    const bool bracketed = host_length >= 2 && host[0] == '[' &&
        host[host_length - 1] == ']';
    for (size_t index = 0; index < host_length; ++index) {
        const char value = host[index];
        if (transport_security_character_is_control(value) ||
            transport_security_character_is_forbidden(value)) {
            return false;
        }
        if (bracketed) {
            const bool ipv6_character =
                (value >= '0' && value <= '9') ||
                (value >= 'a' && value <= 'f') ||
                (value >= 'A' && value <= 'F') ||
                value == ':' || value == '.' || value == '%' ||
                value == '[' || value == ']';
            if (!ipv6_character) {
                return false;
            }
            continue;
        }
        const bool hostname_character =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '.' || value == '-' || value == '_' || value == ':';
        if (!hostname_character) {
            return false;
        }
    }
    return true;
}

static transport_security_error_t transport_security_parse_port(
    const char *port_text,
    size_t port_length,
    uint16_t *port_out
) {
    if (port_length == 0 || port_length > 5) {
        return TRANSPORT_SECURITY_ERR_INVALID_PORT;
    }
    uint32_t port = 0;
    for (size_t index = 0; index < port_length; ++index) {
        const char value = port_text[index];
        if (value < '0' || value > '9') {
            return TRANSPORT_SECURITY_ERR_INVALID_PORT;
        }
        port = port * 10u + (uint32_t)(value - '0');
    }
    if (port == 0 || port > 65535u) {
        return TRANSPORT_SECURITY_ERR_INVALID_PORT;
    }
    *port_out = (uint16_t)port;
    return TRANSPORT_SECURITY_OK;
}

static transport_security_error_t transport_security_validate_policy(
    const transport_security_policy_t *policy
) {
    if (policy == NULL) {
        return TRANSPORT_SECURITY_ERR_NULL_ARGUMENT;
    }
    if (!policy->verify_peer || !policy->verify_hostname) {
        return TRANSPORT_SECURITY_ERR_CERTIFICATE_DISABLED;
    }
    if (policy->allow_plaintext) {
        return TRANSPORT_SECURITY_ERR_CLEARTEXT;
    }
    if (policy->allow_downgrade) {
        return TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE;
    }
    return TRANSPORT_SECURITY_OK;
}

transport_security_error_t transport_security_core_validate_url(
    const char *url,
    const transport_security_policy_t *policy,
    transport_security_endpoint_t *endpoint_out
) {
    if (url == NULL || endpoint_out == NULL) {
        return TRANSPORT_SECURITY_ERR_NULL_ARGUMENT;
    }
    if (url[0] == '\0') {
        return TRANSPORT_SECURITY_ERR_EMPTY_URL;
    }
    const transport_security_error_t policy_result =
        transport_security_validate_policy(policy);
    if (policy_result != TRANSPORT_SECURITY_OK) {
        return policy_result;
    }

    transport_security_protocol_t protocol =
        TRANSPORT_SECURITY_PROTOCOL_UNKNOWN;
    size_t scheme_length = 0;
    if (transport_security_scheme_matches(
            url,
            TRANSPORT_SECURITY_SCHEME_HTTPS,
            TRANSPORT_SECURITY_PROTOCOL_HTTPS,
            &protocol
        )) {
        scheme_length = strlen(TRANSPORT_SECURITY_SCHEME_HTTPS);
    } else if (transport_security_scheme_matches(
                   url,
                   TRANSPORT_SECURITY_SCHEME_WSS,
                   TRANSPORT_SECURITY_PROTOCOL_WSS,
                   &protocol
               )) {
        scheme_length = strlen(TRANSPORT_SECURITY_SCHEME_WSS);
    } else if (transport_security_scheme_matches(
                   url,
                   TRANSPORT_SECURITY_SCHEME_MQTTS,
                   TRANSPORT_SECURITY_PROTOCOL_MQTTS,
                   &protocol
               )) {
        scheme_length = strlen(TRANSPORT_SECURITY_SCHEME_MQTTS);
    } else if (strncmp(url, "http://", 7) == 0 ||
               strncmp(url, "ws://", 5) == 0 ||
               strncmp(url, "mqtt://", 7) == 0) {
        return TRANSPORT_SECURITY_ERR_CLEARTEXT;
    } else {
        return TRANSPORT_SECURITY_ERR_UNSUPPORTED_SCHEME;
    }

    const char *authority = url + scheme_length;
    if (*authority == '\0') {
        return TRANSPORT_SECURITY_ERR_MISSING_HOST;
    }
    const char *authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/' &&
           *authority_end != '?') {
        ++authority_end;
    }
    if (authority_end == authority) {
        return TRANSPORT_SECURITY_ERR_MISSING_HOST;
    }
    if (memchr(authority, '@', (size_t)(authority_end - authority)) != NULL) {
        return TRANSPORT_SECURITY_ERR_EMBEDDED_CREDENTIALS;
    }

    const char *host_end = authority_end;
    const char *port_text = NULL;
    size_t port_length = 0;
    if (authority[0] == '[') {
        const char *closing_bracket = (const char *)memchr(
            authority,
            ']',
            (size_t)(authority_end - authority)
        );
        if (closing_bracket == NULL) {
            return TRANSPORT_SECURITY_ERR_INVALID_HOST;
        }
        host_end = closing_bracket + 1;
        if (host_end < authority_end && *host_end == ':') {
            port_text = host_end + 1;
            port_length = (size_t)(authority_end - port_text);
        } else if (host_end != authority_end) {
            return TRANSPORT_SECURITY_ERR_INVALID_HOST;
        }
    } else {
        const void *colon = memchr(
            authority,
            ':',
            (size_t)(authority_end - authority)
        );
        if (colon != NULL) {
            host_end = (const char *)colon;
            port_text = host_end + 1;
            port_length = (size_t)(authority_end - port_text);
        }
    }

    const size_t host_length = (size_t)(host_end - authority);
    if (!transport_security_host_is_valid(authority, host_length)) {
        return TRANSPORT_SECURITY_ERR_INVALID_HOST;
    }

    uint16_t port = protocol == TRANSPORT_SECURITY_PROTOCOL_MQTTS
        ? 8883
        : 443;
    if (port_text != NULL) {
        const transport_security_error_t port_result =
            transport_security_parse_port(port_text, port_length, &port);
        if (port_result != TRANSPORT_SECURITY_OK) {
            return port_result;
        }
    }

    const char *fragment = strchr(authority_end, '#');
    if (fragment != NULL) {
        return TRANSPORT_SECURITY_ERR_FRAGMENT_NOT_ALLOWED;
    }

    const char *path = authority_end;
    if (*path == '\0') {
        path = "/";
    }

    endpoint_out->protocol = protocol;
    memcpy(endpoint_out->host, authority, host_length);
    endpoint_out->host[host_length] = '\0';
    endpoint_out->port = port;
    endpoint_out->path = path;
    return TRANSPORT_SECURITY_OK;
}

transport_security_error_t transport_security_require_no_downgrade(
    transport_security_protocol_t current,
    transport_security_protocol_t candidate
) {
    if (current == TRANSPORT_SECURITY_PROTOCOL_UNKNOWN ||
        candidate == TRANSPORT_SECURITY_PROTOCOL_UNKNOWN) {
        return TRANSPORT_SECURITY_ERR_UNSUPPORTED_SCHEME;
    }
    if (current != candidate) {
        return TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE;
    }
    return TRANSPORT_SECURITY_OK;
}

const char *transport_security_protocol_name(
    transport_security_protocol_t protocol
) {
    switch (protocol) {
        case TRANSPORT_SECURITY_PROTOCOL_HTTPS:
            return "https";
        case TRANSPORT_SECURITY_PROTOCOL_WSS:
            return "wss";
        case TRANSPORT_SECURITY_PROTOCOL_MQTTS:
            return "mqtts";
        case TRANSPORT_SECURITY_PROTOCOL_UNKNOWN:
        default:
            return "unknown";
    }
}

const char *transport_security_error_name(
    transport_security_error_t error
) {
    switch (error) {
        case TRANSPORT_SECURITY_OK:
            return "ok";
        case TRANSPORT_SECURITY_ERR_NULL_ARGUMENT:
            return "null_argument";
        case TRANSPORT_SECURITY_ERR_EMPTY_URL:
            return "empty_url";
        case TRANSPORT_SECURITY_ERR_UNSUPPORTED_SCHEME:
            return "unsupported_scheme";
        case TRANSPORT_SECURITY_ERR_CLEARTEXT:
            return "cleartext_rejected";
        case TRANSPORT_SECURITY_ERR_MISSING_HOST:
            return "missing_host";
        case TRANSPORT_SECURITY_ERR_INVALID_HOST:
            return "invalid_host";
        case TRANSPORT_SECURITY_ERR_INVALID_PORT:
            return "invalid_port";
        case TRANSPORT_SECURITY_ERR_EMBEDDED_CREDENTIALS:
            return "embedded_credentials";
        case TRANSPORT_SECURITY_ERR_FRAGMENT_NOT_ALLOWED:
            return "fragment_not_allowed";
        case TRANSPORT_SECURITY_ERR_INSECURE_DOWNGRADE:
            return "insecure_downgrade";
        case TRANSPORT_SECURITY_ERR_CERTIFICATE_DISABLED:
            return "certificate_validation_disabled";
        default:
            return "unknown";
    }
}
