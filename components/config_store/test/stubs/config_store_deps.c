#include "config_store_host.h"

#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#define HOST_MAX_ENTRIES 16
#define HOST_MAX_KEY_SIZE 16
#define HOST_MAX_VALUE_SIZE 16384

typedef enum {
    HOST_ENTRY_EMPTY = 0,
    HOST_ENTRY_STRING,
    HOST_ENTRY_BLOB,
} host_entry_type_t;

typedef struct {
    bool used;
    host_entry_type_t type;
    char key[HOST_MAX_KEY_SIZE];
    size_t size;
    uint8_t data[HOST_MAX_VALUE_SIZE];
} host_entry_t;

typedef struct {
    bool initialized;
    host_entry_t entries[HOST_MAX_ENTRIES];
} host_partition_state_t;

static host_partition_state_t host_default_partition;
static host_partition_state_t host_large_partition;

static bool host_large_partition_present;
static esp_err_t host_default_init_result = ESP_OK;
static esp_err_t host_default_retry_result = ESP_OK;
static esp_err_t host_default_erase_result = ESP_OK;
static esp_err_t host_large_init_result = ESP_OK;
static esp_err_t host_large_retry_result = ESP_OK;
static esp_err_t host_large_erase_result = ESP_OK;
static esp_err_t host_default_open_result = ESP_OK;
static esp_err_t host_large_open_result = ESP_OK;
static esp_err_t host_default_set_result = ESP_OK;
static esp_err_t host_large_set_result = ESP_OK;
static esp_err_t host_default_commit_result = ESP_OK;
static esp_err_t host_large_commit_result = ESP_OK;
static esp_err_t host_default_key_erase_result = ESP_OK;
static esp_err_t host_large_key_erase_result = ESP_OK;

static int host_default_init_calls;
static int host_default_erase_calls;
static int host_large_init_calls;
static int host_large_erase_calls;

static host_entry_t *host_find_entry(
    host_partition_state_t *partition,
    const char *key
) {
    if (partition == NULL || key == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < HOST_MAX_ENTRIES; ++index) {
        host_entry_t *entry = &partition->entries[index];
        if (entry->used && strcmp(entry->key, key) == 0) {
            return entry;
        }
    }
    return NULL;
}

static host_entry_t *host_allocate_entry(
    host_partition_state_t *partition,
    const char *key
) {
    host_entry_t *entry = host_find_entry(partition, key);
    if (entry != NULL) {
        entry->size = 0;
        entry->type = HOST_ENTRY_EMPTY;
        return entry;
    }
    for (size_t index = 0; index < HOST_MAX_ENTRIES; ++index) {
        if (!partition->entries[index].used) {
            entry = &partition->entries[index];
            memset(entry, 0, sizeof(*entry));
            entry->used = true;
            memcpy(entry->key, key, strlen(key) + 1);
            return entry;
        }
    }
    return NULL;
}

void config_store_host_reset_state(void);

static void host_reset_partition(host_partition_state_t *partition) {
    memset(partition, 0, sizeof(*partition));
}

void config_store_host_reset(void) {
    host_reset_partition(&host_default_partition);
    host_reset_partition(&host_large_partition);
    host_large_partition_present = true;
    host_default_init_result = ESP_OK;
    host_default_retry_result = ESP_OK;
    host_default_erase_result = ESP_OK;
    host_large_init_result = ESP_OK;
    host_large_retry_result = ESP_OK;
    host_large_erase_result = ESP_OK;
    host_default_open_result = ESP_OK;
    host_large_open_result = ESP_OK;
    host_default_set_result = ESP_OK;
    host_large_set_result = ESP_OK;
    host_default_commit_result = ESP_OK;
    host_large_commit_result = ESP_OK;
    host_default_key_erase_result = ESP_OK;
    host_large_key_erase_result = ESP_OK;
    host_default_init_calls = 0;
    host_default_erase_calls = 0;
    host_large_init_calls = 0;
    host_large_erase_calls = 0;
}

void config_store_host_set_large_partition_present(bool present) {
    host_large_partition_present = present;
}

void config_store_host_set_default_init_result(esp_err_t result) {
    host_default_init_result = result;
}

void config_store_host_set_default_retry_result(esp_err_t result) {
    host_default_retry_result = result;
}

void config_store_host_set_default_erase_result(esp_err_t result) {
    host_default_erase_result = result;
}

void config_store_host_set_large_init_result(esp_err_t result) {
    host_large_init_result = result;
}

void config_store_host_set_large_retry_result(esp_err_t result) {
    host_large_retry_result = result;
}

void config_store_host_set_large_erase_result(esp_err_t result) {
    host_large_erase_result = result;
}

void config_store_host_set_default_open_result(esp_err_t result) {
    host_default_open_result = result;
}

void config_store_host_set_large_open_result(esp_err_t result) {
    host_large_open_result = result;
}

void config_store_host_set_default_set_result(esp_err_t result) {
    host_default_set_result = result;
}

void config_store_host_set_large_set_result(esp_err_t result) {
    host_large_set_result = result;
}

void config_store_host_set_default_commit_result(esp_err_t result) {
    host_default_commit_result = result;
}

void config_store_host_set_large_commit_result(esp_err_t result) {
    host_large_commit_result = result;
}

void config_store_host_set_default_key_erase_result(esp_err_t result) {
    host_default_key_erase_result = result;
}

void config_store_host_set_large_key_erase_result(esp_err_t result) {
    host_large_key_erase_result = result;
}

int config_store_host_default_init_calls(void) {
    return host_default_init_calls;
}

int config_store_host_default_erase_calls(void) {
    return host_default_erase_calls;
}

int config_store_host_large_init_calls(void) {
    return host_large_init_calls;
}

int config_store_host_large_erase_calls(void) {
    return host_large_erase_calls;
}

int config_store_host_key_partition(const char *key) {
    if (host_find_entry(&host_default_partition, key) != NULL) {
        return (int)CONFIG_STORE_HOST_DEFAULT;
    }
    if (host_find_entry(&host_large_partition, key) != NULL) {
        return (int)CONFIG_STORE_HOST_LARGE;
    }
    return -1;
}

bool config_store_host_seed_blob(
    config_store_host_partition_t partition,
    const char *key,
    const void *value,
    size_t value_size
) {
    host_partition_state_t *state = partition == CONFIG_STORE_HOST_LARGE
                                        ? &host_large_partition
                                        : &host_default_partition;
    if (key == NULL || value == NULL || value_size == 0 ||
        strlen(key) >= HOST_MAX_KEY_SIZE ||
        value_size > HOST_MAX_VALUE_SIZE) {
        return false;
    }
    host_entry_t *entry = host_allocate_entry(state, key);
    if (entry == NULL) {
        return false;
    }
    entry->type = HOST_ENTRY_BLOB;
    entry->size = value_size;
    memcpy(entry->data, value, value_size);
    return true;
}

bool config_store_host_seed_string(
    config_store_host_partition_t partition,
    const char *key,
    const char *value
) {
    if (value == NULL) {
        return false;
    }
    host_partition_state_t *state = partition == CONFIG_STORE_HOST_LARGE
                                        ? &host_large_partition
                                        : &host_default_partition;
    const size_t value_size = strlen(value) + 1;
    if (key == NULL || strlen(key) >= HOST_MAX_KEY_SIZE ||
        value_size > HOST_MAX_VALUE_SIZE) {
        return false;
    }
    host_entry_t *entry = host_allocate_entry(state, key);
    if (entry == NULL) {
        return false;
    }
    entry->type = HOST_ENTRY_STRING;
    entry->size = value_size;
    memcpy(entry->data, value, value_size);
    return true;
}

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_NVS_NOT_FOUND:
            return "ESP_ERR_NVS_NOT_FOUND";
        case ESP_ERR_NVS_INVALID_LENGTH:
            return "ESP_ERR_NVS_INVALID_LENGTH";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

esp_err_t nvs_flash_init(void) {
    ++host_default_init_calls;
    const esp_err_t result = host_default_init_calls == 1
                                 ? host_default_init_result
                                 : host_default_retry_result;
    if (result == ESP_OK) {
        host_default_partition.initialized = true;
    }
    return result;
}

esp_err_t nvs_flash_erase(void) {
    ++host_default_erase_calls;
    if (host_default_erase_result != ESP_OK) {
        return host_default_erase_result;
    }
    host_reset_partition(&host_default_partition);
    return ESP_OK;
}

esp_err_t nvs_flash_init_partition(const char *partition_name) {
    ++host_large_init_calls;
    if (partition_name == NULL || strcmp(partition_name, "nvs_data") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = host_large_init_calls == 1
                                 ? host_large_init_result
                                 : host_large_retry_result;
    if (result == ESP_OK) {
        host_large_partition.initialized = true;
    }
    return result;
}

esp_err_t nvs_flash_erase_partition(const char *partition_name) {
    ++host_large_erase_calls;
    if (partition_name == NULL || strcmp(partition_name, "nvs_data") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_large_erase_result != ESP_OK) {
        return host_large_erase_result;
    }
    host_reset_partition(&host_large_partition);
    return ESP_OK;
}

const esp_partition_t *esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char *label
) {
    static const esp_partition_t large = {
        .type = ESP_PARTITION_TYPE_DATA,
        .subtype = ESP_PARTITION_SUBTYPE_DATA_NVS,
        .label = "nvs_data",
    };
    if (!host_large_partition_present ||
        type != ESP_PARTITION_TYPE_DATA ||
        subtype != ESP_PARTITION_SUBTYPE_DATA_NVS ||
        label == NULL ||
        strcmp(label, "nvs_data") != 0) {
        return NULL;
    }
    return &large;
}

esp_err_t nvs_open(
    const char *namespace_name,
    nvs_open_mode_t open_mode,
    nvs_handle_t *out_handle
) {
    (void)namespace_name;
    (void)open_mode;
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_default_open_result != ESP_OK) {
        return host_default_open_result;
    }
    if (!host_default_partition.initialized) {
        return ESP_ERR_NVS_INVALID_STATE;
    }
    *out_handle = 1;
    return ESP_OK;
}

esp_err_t nvs_open_from_partition(
    const char *part_name,
    const char *namespace_name,
    nvs_open_mode_t open_mode,
    nvs_handle_t *out_handle
) {
    (void)namespace_name;
    (void)open_mode;
    if (out_handle == NULL ||
        part_name == NULL ||
        strcmp(part_name, "nvs_data") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_large_open_result != ESP_OK) {
        return host_large_open_result;
    }
    if (!host_large_partition.initialized) {
        return ESP_ERR_NVS_INVALID_STATE;
    }
    *out_handle = 2;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    (void)handle;
}

esp_err_t nvs_set_blob(
    nvs_handle_t handle,
    const char *key,
    const void *value,
    size_t length
) {
    host_partition_state_t *partition = handle == 2
                                            ? &host_large_partition
                                            : &host_default_partition;
    const esp_err_t forced = handle == 2
                                 ? host_large_set_result
                                 : host_default_set_result;
    if (forced != ESP_OK) {
        return forced;
    }
    if (key == NULL || value == NULL || length == 0 ||
        length > HOST_MAX_VALUE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    host_entry_t *entry = host_allocate_entry(partition, key);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }
    entry->type = HOST_ENTRY_BLOB;
    entry->size = length;
    memcpy(entry->data, value, length);
    return ESP_OK;
}

esp_err_t nvs_get_blob(
    nvs_handle_t handle,
    const char *key,
    void *out_value,
    size_t *length
) {
    host_partition_state_t *partition = handle == 2
                                            ? &host_large_partition
                                            : &host_default_partition;
    if (length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    host_entry_t *entry = host_find_entry(partition, key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (entry->type != HOST_ENTRY_BLOB) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    if (out_value == NULL) {
        *length = entry->size;
        return ESP_OK;
    }
    if (*length < entry->size) {
        *length = entry->size;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, entry->data, entry->size);
    *length = entry->size;
    return ESP_OK;
}

esp_err_t nvs_set_str(
    nvs_handle_t handle,
    const char *key,
    const char *value
) {
    host_partition_state_t *partition = handle == 2
                                            ? &host_large_partition
                                            : &host_default_partition;
    const esp_err_t forced = handle == 2
                                 ? host_large_set_result
                                 : host_default_set_result;
    if (forced != ESP_OK) {
        return forced;
    }
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t value_size = strlen(value) + 1;
    if (value_size > HOST_MAX_VALUE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    host_entry_t *entry = host_allocate_entry(partition, key);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }
    entry->type = HOST_ENTRY_STRING;
    entry->size = value_size;
    memcpy(entry->data, value, value_size);
    return ESP_OK;
}

esp_err_t nvs_get_str(
    nvs_handle_t handle,
    const char *key,
    char *out_value,
    size_t *length
) {
    host_partition_state_t *partition = handle == 2
                                            ? &host_large_partition
                                            : &host_default_partition;
    if (length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    host_entry_t *entry = host_find_entry(partition, key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (entry->type != HOST_ENTRY_STRING) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    if (out_value == NULL) {
        *length = entry->size;
        return ESP_OK;
    }
    if (*length < entry->size) {
        *length = entry->size;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, entry->data, entry->size);
    *length = entry->size;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    host_partition_state_t *partition = handle == 2
                                            ? &host_large_partition
                                            : &host_default_partition;
    const esp_err_t forced = handle == 2
                                 ? host_large_key_erase_result
                                 : host_default_key_erase_result;
    if (forced != ESP_OK) {
        return forced;
    }
    host_entry_t *entry = host_find_entry(partition, key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    memset(entry, 0, sizeof(*entry));
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    return handle == 2 ? host_large_commit_result
                       : host_default_commit_result;
}
