#include "device_identity.h"

#include "device_identity_host.h"
#include "esp_err.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const module_descriptor_t *descriptor(void) {
    const module_descriptor_t *value =
        device_identity_module_descriptor();
    assert(value != NULL);
    assert(value->initialize != NULL);
    assert(value->shutdown == NULL);
    return value;
}

static void prepare(void) {
    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    assert(descriptor()->initialize() == ESP_OK);
}

static void test_identifier_is_stable_and_does_not_expose_the_mac(void) {
    prepare();
    const device_identity_t first = device_identity_get();
    assert(strncmp(first.device_id, "device_", 7) == 0);
    assert(strlen(first.device_id) == 23);

    char copied[DEVICE_IDENTIFIER_SIZE] = {0};
    assert(device_identity_copy(copied, sizeof(copied)) == ESP_OK);
    assert(strcmp(copied, first.device_id) == 0);

    device_identity_host_simulate_reboot();
    device_identity_host_reset_state();
    const device_identity_t second = device_identity_get();
    assert(strcmp(first.device_id, second.device_id) == 0);

    const uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    assert(memcmp(first.device_id, mac, sizeof(mac)) != 0);
}

static void test_identifier_changes_with_factory_mac(void) {
    prepare();
    const device_identity_t first = device_identity_get();

    const uint8_t other_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    device_identity_host_set_mac(other_mac);
    device_identity_host_simulate_reboot();
    device_identity_host_reset_state();
    const device_identity_t second = device_identity_get();
    assert(strcmp(first.device_id, second.device_id) != 0);
}

static void test_first_boot_generates_and_persists_a_private_key(void) {
    prepare();
    assert(device_identity_host_generate_calls() == 1);
    assert(device_identity_host_key_persisted());

    uint8_t public_key[DEVICE_IDENTITY_PUBLIC_KEY_BYTES] = {0};
    assert(device_identity_copy_public_key(
        public_key,
        sizeof(public_key)
    ) == ESP_OK);
    assert(public_key[0] == 0x04);
    assert(device_identity_host_generate_calls() == 1);
    assert(device_identity_host_import_calls() == 1);
    assert(device_identity_host_destroy_calls() == 2);
    assert(device_identity_host_key_persisted());
}

static void test_reboot_reuses_the_persisted_key(void) {
    prepare();
    uint8_t first_public_key[DEVICE_IDENTITY_PUBLIC_KEY_BYTES] = {0};
    assert(device_identity_copy_public_key(
        first_public_key,
        sizeof(first_public_key)
    ) == ESP_OK);
    size_t persisted_size = 0;
    const uint8_t *persisted =
        device_identity_host_persisted_key(&persisted_size);
    assert(persisted != NULL);
    assert(persisted_size == 32);

    device_identity_host_simulate_reboot();
    device_identity_host_reset_state();
    uint8_t second_public_key[DEVICE_IDENTITY_PUBLIC_KEY_BYTES] = {0};
    assert(device_identity_copy_public_key(
        second_public_key,
        sizeof(second_public_key)
    ) == ESP_OK);
    assert(memcmp(first_public_key, second_public_key,
                  sizeof(first_public_key)) == 0);
    assert(device_identity_host_generate_calls() == 1);
    assert(device_identity_host_import_calls() == 2);
}

static void test_signature_is_bounded_and_reports_validation_errors(void) {
    prepare();
    static const uint8_t message[] = "challenge";
    uint8_t signature[DEVICE_IDENTITY_SIGNATURE_BYTES] = {0};
    assert(device_identity_sign(
        message,
        sizeof(message) - 1,
        signature,
        sizeof(signature)
    ) == ESP_OK);
    assert(device_identity_host_sign_calls() == 1);
    size_t signed_size = 0;
    const uint8_t *signed_message =
        device_identity_host_last_signed_message(&signed_size);
    assert(signed_size == sizeof(message) - 1);
    assert(memcmp(signed_message, message, signed_size) == 0);

    assert(device_identity_sign(NULL, 1, signature,
                                sizeof(signature)) ==
           ESP_ERR_INVALID_ARG);
    assert(device_identity_sign(message, sizeof(message) - 1, NULL,
                                sizeof(signature)) ==
           ESP_ERR_INVALID_ARG);
    assert(device_identity_sign(message, sizeof(message) - 1, signature,
                                sizeof(signature) - 1) ==
           ESP_ERR_INVALID_SIZE);
}

static void test_output_buffers_and_uninitialized_calls_are_bounded(void) {
    device_identity_host_reset_state();
    device_identity_host_reset();
    assert(device_identity_copy(NULL, 0) == ESP_ERR_INVALID_ARG);
    assert(device_identity_copy_public_key(NULL, 0) ==
           ESP_ERR_INVALID_ARG);
    assert(device_identity_sign(NULL, 0, NULL, 0) ==
           ESP_ERR_INVALID_ARG);

    prepare();
    assert(device_identity_copy((char[DEVICE_IDENTIFIER_SIZE - 1]){0},
                                DEVICE_IDENTIFIER_SIZE - 1) ==
           ESP_ERR_INVALID_SIZE);
    uint8_t short_key[DEVICE_IDENTITY_PUBLIC_KEY_BYTES - 1] = {0};
    assert(device_identity_copy_public_key(short_key,
                                           sizeof(short_key)) ==
           ESP_ERR_INVALID_SIZE);
}

static void test_partition_recovery_reinitializes_after_erase(void) {
    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_set_partition_init_result(ESP_ERR_NVS_NO_FREE_PAGES);
    assert(descriptor()->initialize() == ESP_OK);
    assert(device_identity_host_partition_erase_calls() == 1);
    assert(device_identity_host_partition_init_calls() == 2);
    assert(device_identity_copy_public_key(
        (uint8_t[DEVICE_IDENTITY_PUBLIC_KEY_BYTES]){0},
        DEVICE_IDENTITY_PUBLIC_KEY_BYTES
    ) == ESP_OK);
}

static void test_key_and_crypto_failures_do_not_leak_ready_state(void) {
    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_partition_init_result(ESP_ERR_INVALID_STATE);
    device_identity_host_set_partition_init_persistent_failure(true);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    assert(device_identity_copy(device_id, sizeof(device_id)) ==
           ESP_ERR_INVALID_STATE);
    assert(device_id[0] == '\0');

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_crypto_init_result(-1);
    assert(descriptor()->initialize() == ESP_FAIL);
    assert(device_identity_copy(device_id, sizeof(device_id)) ==
           ESP_ERR_INVALID_STATE);
    assert(device_id[0] == '\0');

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_generate_key_result(-1);
    assert(descriptor()->initialize() == ESP_FAIL);
    assert(!device_identity_host_key_persisted());

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_export_key_result(-1);
    assert(descriptor()->initialize() == ESP_FAIL);
    assert(!device_identity_host_key_persisted());

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_import_key_result(-1);
    assert(descriptor()->initialize() == ESP_FAIL);

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_export_public_key_result(-1);
    assert(descriptor()->initialize() == ESP_FAIL);

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_nvs_set_result(ESP_ERR_INVALID_STATE);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);

    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_clear_persisted_key();
    device_identity_host_set_nvs_commit_result(ESP_ERR_INVALID_STATE);
    assert(descriptor()->initialize() == ESP_ERR_INVALID_STATE);
}

static void test_corrupt_persisted_key_is_rejected(void) {
    device_identity_host_reset_state();
    device_identity_host_reset();
    device_identity_host_set_nvs_get_result(ESP_ERR_NVS_INVALID_LENGTH);
    assert(descriptor()->initialize() == ESP_ERR_NVS_INVALID_LENGTH);
}

static void test_sign_failure_zeroes_the_signature_buffer(void) {
    prepare();
    uint8_t signature[DEVICE_IDENTITY_SIGNATURE_BYTES];
    memset(signature, 0xA5, sizeof(signature));
    device_identity_host_set_sign_result(-1);
    assert(device_identity_sign(
        (const uint8_t *)"challenge",
        9,
        signature,
        sizeof(signature)
    ) == ESP_FAIL);
    for (size_t index = 0; index < sizeof(signature); ++index) {
        assert(signature[index] == 0);
    }
}

int main(void) {
    test_identifier_is_stable_and_does_not_expose_the_mac();
    test_identifier_changes_with_factory_mac();
    test_first_boot_generates_and_persists_a_private_key();
    test_reboot_reuses_the_persisted_key();
    test_signature_is_bounded_and_reports_validation_errors();
    test_output_buffers_and_uninitialized_calls_are_bounded();
    test_partition_recovery_reinitializes_after_erase();
    test_key_and_crypto_failures_do_not_leak_ready_state();
    test_corrupt_persisted_key_is_rejected();
    test_sign_failure_zeroes_the_signature_buffer();
    return 0;
}
