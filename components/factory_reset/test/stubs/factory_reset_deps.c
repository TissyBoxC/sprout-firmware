#include "config_store.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

// Host stand-ins for the NVS partition, config_store, esp_timer, and the state
// mutex. The test drives the clock directly, so the bounded pending timeout is
// exercised without sleeping.
#define FACTORY_RESET_HOST_MAX_ERASED_KEYS 16

static bool host_create_mutex_fails;
static bool host_config_store_ready = true;
static esp_err_t host_config_store_erase_result = ESP_OK;
static esp_err_t host_erase_partition_result = ESP_OK;
static esp_err_t host_init_result = ESP_OK;
static esp_err_t host_timer_create_result = ESP_OK;
static esp_err_t host_timer_start_result = ESP_OK;
static int64_t host_now_ms;

static int host_mutex;
static int host_timer_storage;
static int host_erase_partition_calls;
static int host_init_calls;
static int host_init_partition_calls;
static int host_timer_create_calls;
static int host_timer_start_calls;
static int host_timer_stop_calls;
static int host_timer_delete_calls;
static int host_mutex_delete_calls;
static int host_erased_key_count;
static char host_erased_keys[FACTORY_RESET_HOST_MAX_ERASED_KEYS][32];
static const char *host_last_erased_partition;
static char host_last_erased_partition_storage[32];
static uint64_t host_last_timer_timeout_us;
static esp_timer_cb_t host_timer_callback;
static void *host_timer_argument;

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return host_create_mutex_fails ? NULL : &host_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)semaphore;
    (void)timeout;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    ++host_mutex_delete_calls;
}

bool config_store_is_ready(void) {
    return host_config_store_ready;
}

esp_err_t config_store_erase_key(const char *key) {
    if (host_erased_key_count < FACTORY_RESET_HOST_MAX_ERASED_KEYS &&
        key != NULL) {
        strncpy(
            host_erased_keys[host_erased_key_count],
            key,
            sizeof(host_erased_keys[0]) - 1
        );
    }
    ++host_erased_key_count;
    return host_config_store_erase_result;
}

esp_err_t nvs_flash_erase_partition(const char *partition_label) {
    ++host_erase_partition_calls;
    if (partition_label != NULL) {
        strncpy(
            host_last_erased_partition_storage,
            partition_label,
            sizeof(host_last_erased_partition_storage) - 1
        );
        host_last_erased_partition = host_last_erased_partition_storage;
    }
    return host_erase_partition_result;
}

esp_err_t nvs_flash_init(void) {
    ++host_init_calls;
    return host_init_result;
}

esp_err_t nvs_flash_init_partition(const char *partition_label) {
    (void)partition_label;
    ++host_init_partition_calls;
    return host_init_result;
}

esp_err_t esp_timer_create(
    const esp_timer_create_args_t *args,
    esp_timer_handle_t *out_handle
) {
    ++host_timer_create_calls;
    if (host_timer_create_result != ESP_OK) {
        return host_timer_create_result;
    }
    if (args != NULL) {
        host_timer_callback = args->callback;
        host_timer_argument = args->arg;
    }
    *out_handle = (esp_timer_handle_t)&host_timer_storage;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    (void)timer;
    ++host_timer_start_calls;
    host_last_timer_timeout_us = timeout_us;
    return host_timer_start_result;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    (void)timer;
    ++host_timer_stop_calls;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    (void)timer;
    ++host_timer_delete_calls;
    return ESP_OK;
}

esp_err_t esp_timer_stop_blocking(
    esp_timer_handle_t timer,
    uint32_t timeout_ticks
) {
    (void)timer;
    (void)timeout_ticks;
    ++host_timer_stop_calls;
    return ESP_OK;
}

int64_t esp_timer_get_time(void) {
    return host_now_ms * 1000;
}

const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}

// Test hooks.
void factory_reset_host_reset(void) {
    host_create_mutex_fails = false;
    host_config_store_ready = true;
    host_config_store_erase_result = ESP_OK;
    host_erase_partition_result = ESP_OK;
    host_init_result = ESP_OK;
    host_timer_create_result = ESP_OK;
    host_timer_start_result = ESP_OK;
    host_now_ms = 0;
    host_erase_partition_calls = 0;
    host_init_calls = 0;
    host_init_partition_calls = 0;
    host_timer_create_calls = 0;
    host_timer_start_calls = 0;
    host_timer_stop_calls = 0;
    host_timer_delete_calls = 0;
    host_mutex_delete_calls = 0;
    host_erased_key_count = 0;
    host_last_timer_timeout_us = 0;
    host_last_erased_partition = NULL;
    host_timer_callback = NULL;
    host_timer_argument = NULL;
    memset(host_erased_keys, 0, sizeof(host_erased_keys));
    memset(
        host_last_erased_partition_storage,
        0,
        sizeof(host_last_erased_partition_storage)
    );
}

void factory_reset_host_set_create_mutex_fails(bool fails) {
    host_create_mutex_fails = fails;
}

void factory_reset_host_set_config_store_ready(bool ready) {
    host_config_store_ready = ready;
}

void factory_reset_host_set_config_store_erase_result(esp_err_t result) {
    host_config_store_erase_result = result;
}

void factory_reset_host_set_erase_partition_result(esp_err_t result) {
    host_erase_partition_result = result;
}

void factory_reset_host_set_init_result(esp_err_t result) {
    host_init_result = result;
}

void factory_reset_host_set_timer_create_result(esp_err_t result) {
    host_timer_create_result = result;
}

void factory_reset_host_set_timer_start_result(esp_err_t result) {
    host_timer_start_result = result;
}

void factory_reset_host_advance_ms(int64_t delta_ms) {
    host_now_ms += delta_ms;
}

void factory_reset_host_fire_timer(void) {
    if (host_timer_callback != NULL) {
        host_timer_callback(host_timer_argument);
    }
}

int factory_reset_host_erased_key_count(void) {
    return host_erased_key_count;
}

bool factory_reset_host_erased_key(const char *key) {
    for (int index = 0; index < host_erased_key_count &&
                        index < FACTORY_RESET_HOST_MAX_ERASED_KEYS;
         ++index) {
        if (strcmp(host_erased_keys[index], key) == 0) {
            return true;
        }
    }
    return false;
}

const char *factory_reset_host_last_erased_partition(void) {
    return host_last_erased_partition;
}

uint64_t factory_reset_host_last_timer_timeout_us(void) {
    return host_last_timer_timeout_us;
}

int factory_reset_host_erase_partition_calls(void) {
    return host_erase_partition_calls;
}

int factory_reset_host_init_calls(void) {
    return host_init_calls;
}

int factory_reset_host_timer_create_calls(void) {
    return host_timer_create_calls;
}

int factory_reset_host_timer_start_calls(void) {
    return host_timer_start_calls;
}

int factory_reset_host_timer_stop_calls(void) {
    return host_timer_stop_calls;
}

int factory_reset_host_timer_delete_calls(void) {
    return host_timer_delete_calls;
}

int factory_reset_host_mutex_delete_calls(void) {
    return host_mutex_delete_calls;
}
