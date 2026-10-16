#include "config_store.h"

#include "config_store_host.h"
#include "esp_err.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const module_descriptor_t *descriptor(void) {
    const module_descriptor_t *value = config_store_module_descriptor();
    assert(value != NULL);
    assert(value->initialize != NULL);
    return value;
}

static void prepare(void) {
    config_store_host_reset_state();
    config_store_host_reset();
    assert(config_store_init() == ESP_OK);
    assert(config_store_is_ready());
}

static void test_init_handles_recovery_and_partition_fallback(void) {
    config_store_host_reset_state();
    config_store_host_reset();
    config_store_host_set_default_init_result(ESP_ERR_NVS_NO_FREE_PAGES);
    assert(config_store_init() == ESP_OK);
    assert(config_store_is_ready());
    assert(config_store_host_default_erase_calls() == 1);
    assert(config_store_host_default_init_calls() == 2);
    assert(config_store_host_large_init_calls() == 1);

    config_store_host_reset_state();
    config_store_host_reset();
    config_store_host_set_large_partition_present(false);
    assert(config_store_init() == ESP_OK);
    assert(config_store_is_ready());
    assert(config_store_host_large_init_calls() == 0);

    config_store_host_reset_state();
    config_store_host_reset();
    config_store_host_set_large_init_result(ESP_ERR_NVS_NEW_VERSION_FOUND);
    assert(config_store_init() == ESP_OK);
    assert(config_store_host_large_erase_calls() == 1);
    assert(config_store_host_large_init_calls() == 2);

    config_store_host_reset_state();
    config_store_host_reset();
    config_store_host_set_default_init_result(ESP_ERR_NVS_NO_FREE_PAGES);
    config_store_host_set_default_erase_result(ESP_ERR_INVALID_STATE);
    assert(config_store_init() == ESP_ERR_INVALID_STATE);
    assert(!config_store_is_ready());
}

static void test_strings_round_trip_and_size_errors(void) {
    prepare();
    assert(config_store_set_string("wifi_ssid", "home") == ESP_OK);

    char output[16] = {0};
    assert(config_store_get_string("wifi_ssid", output,
                                   sizeof(output)) == ESP_OK);
    assert(strcmp(output, "home") == 0);

    char short_output[3] = {0};
    assert(config_store_get_string("wifi_ssid", short_output,
                                   sizeof(short_output)) ==
           ESP_ERR_INVALID_SIZE);
    assert(config_store_get_string("wifi_ssid", NULL,
                                   sizeof(output)) == ESP_ERR_INVALID_ARG);
    assert(config_store_get_string("wifi_ssid", output, 0) ==
           ESP_ERR_INVALID_ARG);
}

static void test_blob_routing_uses_large_partition_and_falls_back(void) {
    prepare();
    uint8_t small[32];
    memset(small, 0xAB, sizeof(small));
    assert(config_store_set_blob("small", small, sizeof(small)) == ESP_OK);
    assert(config_store_host_key_partition("small") ==
           CONFIG_STORE_HOST_DEFAULT);

    uint8_t large[CONFIG_STORE_LARGE_BLOB_THRESHOLD + 1];
    memset(large, 0x5A, sizeof(large));
    assert(config_store_set_blob("large", large, sizeof(large)) == ESP_OK);
    assert(config_store_host_key_partition("large") ==
           CONFIG_STORE_HOST_LARGE);

    uint8_t output[sizeof(large)] = {0};
    size_t output_size = sizeof(output);
    assert(config_store_get_blob("large", output, &output_size) == ESP_OK);
    assert(output_size == sizeof(large));
    assert(memcmp(output, large, sizeof(large)) == 0);

    output_size = sizeof(small);
    uint8_t short_output[8] = {0};
    assert(config_store_get_blob("large", short_output, &output_size) ==
           ESP_ERR_INVALID_SIZE);
    assert(output_size == sizeof(large));

    prepare();
    assert(config_store_host_seed_blob(
        CONFIG_STORE_HOST_DEFAULT,
        "large_fallback",
        large,
        sizeof(large)
    ));
    output_size = sizeof(output);
    assert(config_store_get_blob(
        "large_fallback",
        output,
        &output_size
    ) == ESP_OK);
    assert(output_size == sizeof(large));
    assert(memcmp(output, large, sizeof(large)) == 0);
}

static void test_validation_and_not_found_contracts(void) {
    prepare();
    assert(config_store_set_string("", "x") == ESP_ERR_INVALID_ARG);
    assert(config_store_set_string("1234567890123456", "x") ==
           ESP_ERR_INVALID_ARG);
    assert(config_store_set_string("key", NULL) == ESP_ERR_INVALID_ARG);
    assert(config_store_set_blob("key", NULL, 1) == ESP_ERR_INVALID_ARG);
    assert(config_store_set_blob("key", "x", 0) == ESP_ERR_INVALID_ARG);

    char output[8] = {0};
    assert(config_store_get_string("missing", output, sizeof(output)) ==
           CONFIG_STORE_ERR_NOT_FOUND);
    uint8_t blob[8] = {0};
    size_t blob_size = sizeof(blob);
    assert(config_store_get_blob("missing", blob, &blob_size) ==
           CONFIG_STORE_ERR_NOT_FOUND);

    bool has_key = true;
    assert(config_store_has_key("missing", &has_key) == ESP_OK);
    assert(!has_key);
    assert(config_store_erase_key("missing") == ESP_OK);
}

static void test_has_key_checks_value_type_and_large_partition(void) {
    prepare();
    assert(config_store_set_string("string", "value") == ESP_OK);
    uint8_t blob[CONFIG_STORE_LARGE_BLOB_THRESHOLD + 4];
    memset(blob, 0x33, sizeof(blob));
    assert(config_store_set_blob("large", blob, sizeof(blob)) == ESP_OK);

    bool has_key = false;
    assert(config_store_has_key("string", &has_key) == ESP_OK);
    assert(has_key);
    assert(config_store_has_key("large", &has_key) == ESP_OK);
    assert(has_key);
    assert(config_store_has_key("absent", &has_key) == ESP_OK);
    assert(!has_key);
}

static void test_erase_removes_from_both_partitions(void) {
    prepare();
    uint8_t value[CONFIG_STORE_LARGE_BLOB_THRESHOLD + 8];
    memset(value, 0x77, sizeof(value));
    assert(config_store_set_blob("both", value, sizeof(value)) == ESP_OK);
    assert(config_store_set_string("both_str", "value") == ESP_OK);

    assert(config_store_erase_key("both") == ESP_OK);
    assert(config_store_host_key_partition("both") == -1);
    assert(config_store_erase_key("both_str") == ESP_OK);
    assert(config_store_host_key_partition("both_str") == -1);

    config_store_host_set_default_key_erase_result(ESP_ERR_INVALID_STATE);
    assert(config_store_erase_key("missing") == ESP_ERR_INVALID_STATE);
}

static void test_open_and_commit_failures_are_propagated(void) {
    prepare();
    config_store_host_set_default_open_result(ESP_ERR_INVALID_STATE);
    assert(config_store_set_string("key", "value") == ESP_ERR_INVALID_STATE);
    assert(config_store_get_string("key", (char[8]){0}, 8) ==
           ESP_ERR_INVALID_STATE);

    config_store_host_set_default_open_result(ESP_OK);
    config_store_host_set_default_commit_result(ESP_ERR_INVALID_STATE);
    assert(config_store_set_string("key", "value") == ESP_ERR_INVALID_STATE);

    config_store_host_set_default_commit_result(ESP_OK);
    config_store_host_set_default_set_result(ESP_ERR_NO_MEM);
    assert(config_store_set_string("key", "value") == ESP_ERR_NO_MEM);

    prepare();
    config_store_host_set_large_open_result(ESP_ERR_INVALID_STATE);
    uint8_t large[CONFIG_STORE_LARGE_BLOB_THRESHOLD + 1];
    memset(large, 0x11, sizeof(large));
    assert(config_store_set_blob("large", large, sizeof(large)) ==
           ESP_ERR_INVALID_STATE);
}

static void test_descriptor_and_idempotent_init(void) {
    prepare();
    assert(config_store_init() == ESP_OK);
    assert(config_store_host_default_init_calls() == 1);

    const module_descriptor_t *value = descriptor();
    assert(strcmp(value->module_name, "config_store") == 0);
    assert(strcmp(value->version, "1.0.0") == 0);
    assert(value->shutdown == NULL);
}

int main(void) {
    test_init_handles_recovery_and_partition_fallback();
    test_strings_round_trip_and_size_errors();
    test_blob_routing_uses_large_partition_and_falls_back();
    test_validation_and_not_found_contracts();
    test_has_key_checks_value_type_and_large_partition();
    test_erase_removes_from_both_partitions();
    test_open_and_commit_failures_are_propagated();
    test_descriptor_and_idempotent_init();
    return 0;
}
