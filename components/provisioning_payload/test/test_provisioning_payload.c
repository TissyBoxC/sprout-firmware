#include "provisioning_payload.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

// UTF-8 bytes for the Chinese product name, kept as escapes so this source
// stays ASCII while still exercising multi-byte percent-encoding.
static const char *const kChineseDeviceName =
    "\xE5\xB0\x8F\xE8\x90\x8C\xE8\x8A\xBD";

static void test_static_contract_values(void) {
    assert(strcmp(provisioning_payload_scheme(), "sprout") == 0);
    assert(
        strcmp(provisioning_payload_security_username(), "sprout-provisioning")
            == 0
    );
}

static void test_setup_qr_round_trip(void) {
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};
    char service_name[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};
    char pop[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};

    assert(
        provisioning_payload_build_setup_qr(
            "sprout_1234",
            "pop-abc.def:1",
            payload,
            sizeof(payload)
        ) == ESP_OK
    );
    /* The setup URI never carries a platform token. */
    assert(strstr(payload, "token=") == NULL);
    assert(strncmp(payload, "sprout://setup?", 15) == 0);

    assert(
        provisioning_payload_parse_setup_qr(
            payload,
            service_name,
            sizeof(service_name),
            pop,
            sizeof(pop)
        ) == ESP_OK
    );
    assert(strcmp(service_name, "sprout_1234") == 0);
    assert(strcmp(pop, "pop-abc.def:1") == 0);
}

static void test_setup_qr_rejects_unsafe_identifiers(void) {
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};
    /* A space, ampersand or percent would break the unescaped segment. */
    assert(
        provisioning_payload_build_setup_qr(
            "sprout 1234",
            "pop",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_build_setup_qr(
            "sprout1234",
            "pop&user=attacker",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_build_setup_qr(
            NULL,
            "pop",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_ARG
    );
}

static void test_setup_qr_reports_small_buffer(void) {
    char payload[8] = {0};
    assert(
        provisioning_payload_build_setup_qr(
            "sprout1234",
            "pop",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_SIZE
    );
    /* A failed build must not leave a partial payload behind. */
    assert(payload[0] == '\0');
}

static void test_setup_qr_parse_rejects_malformed_input(void) {
    char service_name[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};
    char pop[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};
    const char *wrong_host = "sprout://device?service=a&pop=b";
    const char *missing_pop = "sprout://setup?service=sprout1234";
    const char *empty_pop = "sprout://setup?service=sprout1234&pop=";

    assert(
        provisioning_payload_parse_setup_qr(
            wrong_host,
            service_name,
            sizeof(service_name),
            pop,
            sizeof(pop)
        ) == ESP_ERR_INVALID_ARG
    );

    /* A missing pop field is not a valid setup payload. */
    assert(
        provisioning_payload_parse_setup_qr(
            missing_pop,
            service_name,
            sizeof(service_name),
            pop,
            sizeof(pop)
        ) == ESP_ERR_INVALID_ARG
    );

    /* An empty field is treated as absent. */
    assert(
        provisioning_payload_parse_setup_qr(
            empty_pop,
            service_name,
            sizeof(service_name),
            pop,
            sizeof(pop)
        ) == ESP_ERR_INVALID_ARG
    );
}

static void test_binding_qr_round_trip(void) {
    const char *const token =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};
    char device_id[32] = {0};
    /* The platform binding token is 64 hex characters plus the terminator. */
    char binding_token[128] = {0};

    assert(
        provisioning_payload_build_qr(
            "dev-0001",
            token,
            kChineseDeviceName,
            payload,
            sizeof(payload)
        ) == ESP_OK
    );
    assert(
        provisioning_payload_parse(
            payload,
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_OK
    );
    assert(strcmp(device_id, "dev-0001") == 0);
    assert(strcmp(binding_token, token) == 0);
}

static void test_binding_qr_percent_encodes_display_name(void) {
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};

    /* The Chinese product name must be percent-encoded, not emitted raw. */
    assert(
        provisioning_payload_build_qr(
            "dev-1",
            "tok-1",
            kChineseDeviceName,
            payload,
            sizeof(payload)
        ) == ESP_OK
    );
    assert(strstr(payload, "%E5%B0%8F%E8%90%8C%E8%8A%BD") != NULL);

    /* A missing name is allowed and encodes as an empty value. */
    assert(
        provisioning_payload_build_qr(
            "dev-1",
            "tok-1",
            NULL,
            payload,
            sizeof(payload)
        ) == ESP_OK
    );
    assert(strstr(payload, "name=") != NULL);
}

static void test_binding_qr_rejects_bad_input(void) {
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};
    assert(
        provisioning_payload_build_qr(
            "dev 1",
            "tok-1",
            "name",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_build_qr(
            "dev-1",
            "tok 1",
            "name",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_build_qr("dev-1", "tok-1", "name", NULL, 0)
            == ESP_ERR_INVALID_ARG
    );
}

static void test_binding_qr_reports_small_buffer(void) {
    char payload[8] = {0};
    assert(
        provisioning_payload_build_qr(
            "dev-1",
            "tok-1",
            "name",
            payload,
            sizeof(payload)
        ) == ESP_ERR_INVALID_SIZE
    );
    assert(payload[0] == '\0');
}

static void test_binding_parse_rejects_malformed_input(void) {
    char device_id[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};
    char binding_token[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};

    /* Wrong host: the setup host is not a binding payload. */
    assert(
        provisioning_payload_parse(
            "sprout://setup?device_id=dev-1&token=tok-1",
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );

    /* Missing token. */
    assert(
        provisioning_payload_parse(
            "sprout://device?device_id=dev-1",
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );

    /* Empty token is treated as absent. */
    assert(
        provisioning_payload_parse(
            "sprout://device?device_id=dev-1&token=",
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );
}

static void test_extract_rejects_undersized_output(void) {
    char device_id[4] = {0};
    char binding_token[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};
    const char *const payload =
        "sprout://device?device_id=dev-0001&token=tok-1";
    assert(
        provisioning_payload_parse(
            payload,
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );
}

static void test_null_arguments_are_rejected(void) {
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};
    char device_id[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};
    char binding_token[PROVISIONING_PAYLOAD_USERNAME_SIZE] = {0};

    assert(
        provisioning_payload_parse(
            NULL,
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_parse(
            "sprout://device?device_id=a&token=b",
            NULL,
            0,
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_parse_setup_qr(
            NULL,
            device_id,
            sizeof(device_id),
            binding_token,
            sizeof(binding_token)
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        provisioning_payload_build_qr("dev-1", "tok-1", "name", payload, 0)
            == ESP_ERR_INVALID_ARG
    );
}

static void test_module_descriptor_is_stable(void) {
    const module_descriptor_t *descriptor =
        provisioning_payload_module_descriptor();
    assert(descriptor != NULL);
    assert(strcmp(descriptor->module_name, "provisioning_payload") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != NULL);
    assert(descriptor->initialize() == ESP_OK);
    assert(descriptor->shutdown == NULL);
}

int main(void) {
    test_static_contract_values();
    test_setup_qr_round_trip();
    test_setup_qr_rejects_unsafe_identifiers();
    test_setup_qr_reports_small_buffer();
    test_setup_qr_parse_rejects_malformed_input();
    test_binding_qr_round_trip();
    test_binding_qr_percent_encodes_display_name();
    test_binding_qr_rejects_bad_input();
    test_binding_qr_reports_small_buffer();
    test_binding_parse_rejects_malformed_input();
    test_extract_rejects_undersized_output();
    test_null_arguments_are_rejected();
    test_module_descriptor_is_stable();
    return 0;
}
