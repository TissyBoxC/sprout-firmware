#include "device_identity_host.h"

#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

#include <string.h>

#define HOST_MAC_SIZE 6
#define HOST_PRIVATE_KEY_SIZE 32
#define HOST_PUBLIC_KEY_SIZE 65
#define HOST_SIGNATURE_SIZE 64
#define HOST_DIGEST_SIZE 32
#define HOST_KEY_SLOT_COUNT 4
#define HOST_MAX_MESSAGE_SIZE 1024

typedef struct {
    bool used;
    bool has_private_key;
    bool has_public_key;
    uint8_t private_key[HOST_PRIVATE_KEY_SIZE];
    uint8_t public_key[HOST_PUBLIC_KEY_SIZE];
} host_key_slot_t;

static host_key_slot_t host_keys[HOST_KEY_SLOT_COUNT];
static uint8_t host_persisted_private_key[HOST_PRIVATE_KEY_SIZE];
static bool host_persisted;
static uint8_t host_mac[HOST_MAC_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44,
                                           0x55};
static uint8_t host_hash_digest[HOST_DIGEST_SIZE];
static uint8_t host_last_hash_input[HOST_MAC_SIZE];
static size_t host_last_hash_input_size;
static uint8_t host_last_message[HOST_MAX_MESSAGE_SIZE];
static size_t host_last_message_size;
static uint8_t host_generated_key_seed;

static int host_crypto_init_result = PSA_SUCCESS;
static esp_err_t host_partition_init_result = ESP_OK;
static esp_err_t host_partition_init_retry_result = ESP_OK;
static bool host_partition_init_persistent_failure;
static esp_err_t host_partition_erase_result = ESP_OK;
static esp_err_t host_nvs_open_result = ESP_OK;
static esp_err_t host_nvs_get_result = ESP_OK;
static esp_err_t host_nvs_set_result = ESP_OK;
static esp_err_t host_nvs_commit_result = ESP_OK;
static int host_generate_key_result = PSA_SUCCESS;
static int host_export_key_result = PSA_SUCCESS;
static size_t host_export_key_size = HOST_PRIVATE_KEY_SIZE;
static int host_import_key_result = PSA_SUCCESS;
static int host_export_public_key_result = PSA_SUCCESS;
static size_t host_export_public_key_size = HOST_PUBLIC_KEY_SIZE;
static int host_hash_result = PSA_SUCCESS;
static int host_sign_result = PSA_SUCCESS;
static size_t host_sign_size = HOST_SIGNATURE_SIZE;

static int host_generate_calls;
static int host_import_calls;
static int host_sign_calls;
static int host_destroy_calls;
static int host_partition_init_calls;
static int host_partition_erase_calls;

static host_key_slot_t *host_allocate_slot(void) {
    for (size_t index = 0; index < HOST_KEY_SLOT_COUNT; ++index) {
        if (!host_keys[index].used) {
            memset(&host_keys[index], 0, sizeof(host_keys[index]));
            host_keys[index].used = true;
            return &host_keys[index];
        }
    }
    return NULL;
}

static host_key_slot_t *host_slot_for_key(psa_key_id_t key) {
    const size_t index = (size_t)(key - 1);
    if (index >= HOST_KEY_SLOT_COUNT || !host_keys[index].used) {
        return NULL;
    }
    return &host_keys[index];
}

static void host_initialize_public_key(host_key_slot_t *slot) {
    memset(slot->public_key, 0, sizeof(slot->public_key));
    slot->public_key[0] = 0x04;
    for (size_t index = 0; index < HOST_PRIVATE_KEY_SIZE; ++index) {
        slot->public_key[1 + index] =
            (uint8_t)(slot->private_key[index] ^ 0x5AU);
        slot->public_key[1 + HOST_PRIVATE_KEY_SIZE + index] =
            (uint8_t)(slot->private_key[index] ^ 0xA5U);
    }
    slot->has_public_key = true;
}

void device_identity_host_reset_state(void);

void device_identity_host_reset(void) {
    memset(host_keys, 0, sizeof(host_keys));
    memset(host_hash_digest, 0, sizeof(host_hash_digest));
    for (size_t index = 0; index < sizeof(host_hash_digest); ++index) {
        host_hash_digest[index] = (uint8_t)(0xA0U + index);
    }
    memset(host_last_message, 0, sizeof(host_last_message));
    host_last_message_size = 0;
    memset(host_last_hash_input, 0, sizeof(host_last_hash_input));
    host_last_hash_input_size = 0;
    host_generated_key_seed = 0x40;
    host_crypto_init_result = PSA_SUCCESS;
    host_partition_init_result = ESP_OK;
    host_partition_init_retry_result = ESP_OK;
    host_partition_init_persistent_failure = false;
    host_partition_erase_result = ESP_OK;
    host_nvs_open_result = ESP_OK;
    host_nvs_get_result = ESP_OK;
    host_nvs_set_result = ESP_OK;
    host_nvs_commit_result = ESP_OK;
    host_generate_key_result = PSA_SUCCESS;
    host_export_key_result = PSA_SUCCESS;
    host_export_key_size = HOST_PRIVATE_KEY_SIZE;
    host_import_key_result = PSA_SUCCESS;
    host_export_public_key_result = PSA_SUCCESS;
    host_export_public_key_size = HOST_PUBLIC_KEY_SIZE;
    host_hash_result = PSA_SUCCESS;
    host_sign_result = PSA_SUCCESS;
    host_sign_size = HOST_SIGNATURE_SIZE;
    host_generate_calls = 0;
    host_import_calls = 0;
    host_sign_calls = 0;
    host_destroy_calls = 0;
    host_partition_init_calls = 0;
    host_partition_erase_calls = 0;
}

void device_identity_host_simulate_reboot(void) {
    memset(host_keys, 0, sizeof(host_keys));
}

void device_identity_host_clear_persisted_key(void) {
    memset(host_persisted_private_key, 0,
           sizeof(host_persisted_private_key));
    host_persisted = false;
}

void device_identity_host_set_mac(const uint8_t mac[6]) {
    if (mac != NULL) {
        memcpy(host_mac, mac, sizeof(host_mac));
    }
}

void device_identity_host_set_crypto_init_result(int result) {
    host_crypto_init_result = result;
}

void device_identity_host_set_partition_init_result(esp_err_t result) {
    host_partition_init_result = result;
}

void device_identity_host_set_partition_init_retry_result(esp_err_t result) {
    host_partition_init_retry_result = result;
}

void device_identity_host_set_partition_init_persistent_failure(
    bool persistent
) {
    host_partition_init_persistent_failure = persistent;
}

void device_identity_host_set_partition_erase_result(esp_err_t result) {
    host_partition_erase_result = result;
}

void device_identity_host_set_nvs_open_result(esp_err_t result) {
    host_nvs_open_result = result;
}

void device_identity_host_set_nvs_get_result(esp_err_t result) {
    host_nvs_get_result = result;
}

void device_identity_host_set_nvs_set_result(esp_err_t result) {
    host_nvs_set_result = result;
}

void device_identity_host_set_nvs_commit_result(esp_err_t result) {
    host_nvs_commit_result = result;
}

void device_identity_host_set_generate_key_result(int result) {
    host_generate_key_result = result;
}

void device_identity_host_set_export_key_result(int result) {
    host_export_key_result = result;
}

void device_identity_host_set_export_key_size(size_t size) {
    host_export_key_size = size;
}

void device_identity_host_set_import_key_result(int result) {
    host_import_key_result = result;
}

void device_identity_host_set_export_public_key_result(int result) {
    host_export_public_key_result = result;
}

void device_identity_host_set_export_public_key_size(size_t size) {
    host_export_public_key_size = size;
}

void device_identity_host_set_hash_result(int result) {
    host_hash_result = result;
}

void device_identity_host_set_hash_digest(
    const uint8_t *digest,
    size_t size
) {
    if (digest != NULL && size == sizeof(host_hash_digest)) {
        memcpy(host_hash_digest, digest, size);
    }
}

void device_identity_host_set_sign_result(int result) {
    host_sign_result = result;
}

void device_identity_host_set_sign_size(size_t size) {
    host_sign_size = size;
}

bool device_identity_host_key_persisted(void) {
    return host_persisted;
}

const uint8_t *device_identity_host_persisted_key(size_t *size_out) {
    if (size_out != NULL) {
        *size_out = host_persisted ? HOST_PRIVATE_KEY_SIZE : 0;
    }
    return host_persisted ? host_persisted_private_key : NULL;
}

int device_identity_host_generate_calls(void) {
    return host_generate_calls;
}

int device_identity_host_import_calls(void) {
    return host_import_calls;
}

int device_identity_host_sign_calls(void) {
    return host_sign_calls;
}

int device_identity_host_destroy_calls(void) {
    return host_destroy_calls;
}

int device_identity_host_partition_init_calls(void) {
    return host_partition_init_calls;
}

int device_identity_host_partition_erase_calls(void) {
    return host_partition_erase_calls;
}

const uint8_t *device_identity_host_last_signed_message(
    size_t *size_out
) {
    if (size_out != NULL) {
        *size_out = host_last_message_size;
    }
    return host_last_message;
}

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_ERR_NVS_NO_FREE_PAGES:
            return "ESP_ERR_NVS_NO_FREE_PAGES";
        case ESP_ERR_NVS_NEW_VERSION_FOUND:
            return "ESP_ERR_NVS_NEW_VERSION_FOUND";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

esp_err_t esp_efuse_mac_get_default(uint8_t mac[6]) {
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(mac, host_mac, HOST_MAC_SIZE);
    return ESP_OK;
}

esp_err_t nvs_flash_init_partition(const char *partition_name) {
    if (partition_name == NULL ||
        strcmp(partition_name, "nvs") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ++host_partition_init_calls;
    const esp_err_t result = host_partition_init_persistent_failure
                                 ? host_partition_init_result
                                 : (host_partition_init_calls == 1
                                        ? host_partition_init_result
                                        : host_partition_init_retry_result);
    return result;
}

esp_err_t nvs_flash_erase_partition(const char *partition_name) {
    if (partition_name == NULL ||
        strcmp(partition_name, "nvs") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ++host_partition_erase_calls;
    if (host_partition_erase_result != ESP_OK) {
        return host_partition_erase_result;
    }
    host_persisted = false;
    memset(host_persisted_private_key, 0,
           sizeof(host_persisted_private_key));
    return ESP_OK;
}

esp_err_t nvs_open_from_partition(
    const char *part_name,
    const char *namespace_name,
    nvs_open_mode_t open_mode,
    nvs_handle_t *out_handle
) {
    (void)namespace_name;
    if (out_handle == NULL || part_name == NULL ||
        strcmp(part_name, "nvs") != 0 ||
        open_mode != NVS_READWRITE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_nvs_open_result != ESP_OK) {
        return host_nvs_open_result;
    }
    *out_handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    (void)handle;
}

esp_err_t nvs_get_blob(
    nvs_handle_t handle,
    const char *key,
    void *out_value,
    size_t *length
) {
    (void)handle;
    if (key == NULL || strcmp(key, "p256_private") != 0 ||
        length == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_nvs_get_result != ESP_OK) {
        return host_nvs_get_result;
    }
    if (!host_persisted) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*length < sizeof(host_persisted_private_key)) {
        *length = sizeof(host_persisted_private_key);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, host_persisted_private_key,
           sizeof(host_persisted_private_key));
    *length = sizeof(host_persisted_private_key);
    return ESP_OK;
}

esp_err_t nvs_set_blob(
    nvs_handle_t handle,
    const char *key,
    const void *value,
    size_t length
) {
    (void)handle;
    if (key == NULL || strcmp(key, "p256_private") != 0 ||
        value == NULL || length != sizeof(host_persisted_private_key)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_nvs_set_result != ESP_OK) {
        return host_nvs_set_result;
    }
    memcpy(host_persisted_private_key, value,
           sizeof(host_persisted_private_key));
    host_persisted = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return host_nvs_commit_result;
}

void psa_set_key_type(psa_key_attributes_t *attributes, psa_key_type_t type) {
    if (attributes != NULL) {
        attributes->type = type;
    }
}

void psa_set_key_bits(psa_key_attributes_t *attributes, size_t bits) {
    if (attributes != NULL) {
        attributes->bits = bits;
    }
}

void psa_set_key_usage_flags(
    psa_key_attributes_t *attributes,
    psa_key_usage_t usage
) {
    if (attributes != NULL) {
        attributes->usage = usage;
    }
}

void psa_set_key_algorithm(
    psa_key_attributes_t *attributes,
    psa_algorithm_t algorithm
) {
    if (attributes != NULL) {
        attributes->algorithm = algorithm;
    }
}

psa_status_t psa_crypto_init(void) {
    return host_crypto_init_result;
}

psa_status_t psa_generate_key(
    const psa_key_attributes_t *attributes,
    psa_key_id_t *key
) {
    (void)attributes;
    ++host_generate_calls;
    if (host_generate_key_result != PSA_SUCCESS) {
        return host_generate_key_result;
    }
    if (key == NULL) {
        return -1;
    }
    host_key_slot_t *slot = host_allocate_slot();
    if (slot == NULL) {
        return -1;
    }
    for (size_t index = 0; index < HOST_PRIVATE_KEY_SIZE; ++index) {
        slot->private_key[index] =
            (uint8_t)(host_generated_key_seed + index);
    }
    ++host_generated_key_seed;
    slot->has_private_key = true;
    host_initialize_public_key(slot);
    *key = (psa_key_id_t)((slot - host_keys) + 1);
    return PSA_SUCCESS;
}

psa_status_t psa_export_key(
    psa_key_id_t key,
    uint8_t *data,
    size_t data_size,
    size_t *data_length
) {
    if (host_export_key_result != PSA_SUCCESS) {
        return host_export_key_result;
    }
    host_key_slot_t *slot = host_slot_for_key(key);
    if (slot == NULL || !slot->has_private_key || data == NULL ||
        data_length == NULL) {
        return -1;
    }
    if (data_size < host_export_key_size) {
        *data_length = host_export_key_size;
        return -1;
    }
    memcpy(data, slot->private_key, HOST_PRIVATE_KEY_SIZE);
    *data_length = host_export_key_size;
    return PSA_SUCCESS;
}

psa_status_t psa_import_key(
    const psa_key_attributes_t *attributes,
    const uint8_t *data,
    size_t data_size,
    psa_key_id_t *key
) {
    (void)attributes;
    ++host_import_calls;
    if (host_import_key_result != PSA_SUCCESS) {
        return host_import_key_result;
    }
    if (data == NULL || data_size != HOST_PRIVATE_KEY_SIZE || key == NULL) {
        return -1;
    }
    host_key_slot_t *slot = host_allocate_slot();
    if (slot == NULL) {
        return -1;
    }
    memcpy(slot->private_key, data, HOST_PRIVATE_KEY_SIZE);
    slot->has_private_key = true;
    host_initialize_public_key(slot);
    *key = (psa_key_id_t)((slot - host_keys) + 1);
    return PSA_SUCCESS;
}

psa_status_t psa_export_public_key(
    psa_key_id_t key,
    uint8_t *data,
    size_t data_size,
    size_t *data_length
) {
    if (host_export_public_key_result != PSA_SUCCESS) {
        return host_export_public_key_result;
    }
    host_key_slot_t *slot = host_slot_for_key(key);
    if (slot == NULL || !slot->has_public_key || data == NULL ||
        data_length == NULL) {
        return -1;
    }
    if (data_size < host_export_public_key_size) {
        *data_length = host_export_public_key_size;
        return -1;
    }
    memcpy(data, slot->public_key, HOST_PUBLIC_KEY_SIZE);
    *data_length = host_export_public_key_size;
    return PSA_SUCCESS;
}

psa_status_t psa_hash_compute(
    psa_algorithm_t alg,
    const uint8_t *input,
    size_t input_length,
    uint8_t *hash,
    size_t hash_size,
    size_t *hash_length
) {
    (void)alg;
    if (host_hash_result != PSA_SUCCESS) {
        return host_hash_result;
    }
    if (hash == NULL || hash_length == NULL ||
        hash_size < sizeof(host_hash_digest) ||
        input == NULL || input_length > sizeof(host_last_hash_input)) {
        return -1;
    }
    memcpy(host_last_hash_input, input, input_length);
    host_last_hash_input_size = input_length;
    memcpy(hash, host_hash_digest, sizeof(host_hash_digest));
    // Make the digest depend on the MAC so identity changes are observable
    // without pretending to implement SHA-256 in the host stub.
    for (size_t index = 0; index < input_length; ++index) {
        hash[index] ^= input[index];
    }
    *hash_length = sizeof(host_hash_digest);
    return PSA_SUCCESS;
}

psa_status_t psa_sign_message(
    psa_key_id_t key,
    psa_algorithm_t alg,
    const uint8_t *input,
    size_t input_length,
    uint8_t *signature,
    size_t signature_size,
    size_t *signature_length
) {
    (void)alg;
    ++host_sign_calls;
    if (host_sign_result != PSA_SUCCESS) {
        return host_sign_result;
    }
    host_key_slot_t *slot = host_slot_for_key(key);
    if (slot == NULL || input == NULL || signature == NULL ||
        signature_length == NULL || input_length > HOST_MAX_MESSAGE_SIZE) {
        return -1;
    }
    memcpy(host_last_message, input, input_length);
    host_last_message_size = input_length;
    if (signature_size < host_sign_size) {
        *signature_length = host_sign_size;
        return -1;
    }
    for (size_t index = 0; index < HOST_SIGNATURE_SIZE; ++index) {
        signature[index] = (uint8_t)(
            slot->private_key[index % HOST_PRIVATE_KEY_SIZE] ^
            input[index % input_length]
        );
    }
    *signature_length = host_sign_size;
    return PSA_SUCCESS;
}

psa_status_t psa_destroy_key(psa_key_id_t key) {
    ++host_destroy_calls;
    host_key_slot_t *slot = host_slot_for_key(key);
    if (slot != NULL) {
        memset(slot, 0, sizeof(*slot));
    }
    return PSA_SUCCESS;
}
