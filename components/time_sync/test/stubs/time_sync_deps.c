#include "time_sync_host.h"

#include <stddef.h>
#include <string.h>
#include <sys/time.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "network_manager.h"

int settimeofday(const struct timeval *value, const struct timezone *timezone);

#define TIME_SYNC_HOST_VALUE_COUNT 8
#define TIME_SYNC_HOST_VALUE_SIZE 64

typedef struct {
    char key[32];
    char value[TIME_SYNC_HOST_VALUE_SIZE];
    esp_err_t set_result;
    int set_calls;
} host_value_t;

static host_value_t host_values[TIME_SYNC_HOST_VALUE_COUNT];
static size_t host_value_count;
static bool host_store_ready;
static int64_t host_clock_epoch;
static int host_clock_result;
static int host_clock_calls;
static network_manager_state_t host_network_state;
static network_manager_state_callback_t host_network_callback;
static void *host_network_context;
static esp_err_t host_callback_result;
static esp_sntp_time_sync_notification_cb_t host_sntp_callback;
static esp_sntp_operatingmode_t host_sntp_mode;
static sntp_sync_status_t host_sntp_status;
static char host_sntp_server[128];
static int host_sntp_init_calls;
static int host_sntp_stop_calls;
static int host_sntp_restart_calls;

static host_value_t *host_find_value(const char *key) {
    for (size_t index = 0; index < host_value_count; index++) {
        if (strcmp(host_values[index].key, key) == 0) {
            return &host_values[index];
        }
    }
    return NULL;
}

static host_value_t *host_add_value(const char *key) {
    host_value_t *value = host_find_value(key);
    if (value != NULL) {
        return value;
    }
    if (host_value_count >= TIME_SYNC_HOST_VALUE_COUNT) {
        return NULL;
    }
    value = &host_values[host_value_count++];
    memset(value, 0, sizeof(*value));
    strncpy(value->key, key, sizeof(value->key) - 1);
    return value;
}

void time_sync_host_reset(void) {
    memset(host_values, 0, sizeof(host_values));
    host_value_count = 0;
    host_store_ready = true;
    host_clock_epoch = 0;
    host_clock_result = 0;
    host_clock_calls = 0;
    host_network_state = NETWORK_MANAGER_STATE_IDLE;
    host_network_callback = NULL;
    host_network_context = NULL;
    host_callback_result = ESP_OK;
    host_sntp_callback = NULL;
    host_sntp_mode = ESP_SNTP_OPMODE_POLL;
    host_sntp_status = SNTP_SYNC_STATUS_RESET;
    host_sntp_server[0] = '\0';
    host_sntp_init_calls = 0;
    host_sntp_stop_calls = 0;
    host_sntp_restart_calls = 0;
}

void time_sync_host_set_store_ready(bool ready) {
    host_store_ready = ready;
}

void time_sync_host_set_string(const char *key, const char *value) {
    host_value_t *entry = host_add_value(key);
    if (entry == NULL) {
        return;
    }
    strncpy(
        entry->value,
        value != NULL ? value : "",
        sizeof(entry->value) - 1
    );
    entry->value[sizeof(entry->value) - 1] = '\0';
}

void time_sync_host_set_set_result(const char *key, esp_err_t result) {
    host_value_t *entry = host_add_value(key);
    if (entry != NULL) {
        entry->set_result = result;
    }
}

void time_sync_host_set_clock_result(int result) {
    host_clock_result = result;
}

void time_sync_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void time_sync_host_set_callback_result(esp_err_t result) {
    host_callback_result = result;
}

esp_err_t time_sync_host_fire_network_state(network_manager_state_t state) {
    if (host_network_callback == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    host_network_callback(state, host_network_context);
    return ESP_OK;
}

void time_sync_host_fire_sntp(int64_t epoch_seconds) {
    if (host_sntp_callback == NULL) {
        return;
    }
    struct timeval value = {
        .tv_sec = (time_t)epoch_seconds,
        .tv_usec = 0,
    };
    host_sntp_callback(&value);
}

int64_t time_sync_host_clock_epoch(void) {
    return host_clock_epoch;
}

int time_sync_host_clock_calls(void) {
    return host_clock_calls;
}

int time_sync_host_sntp_init_calls(void) {
    return host_sntp_init_calls;
}

int time_sync_host_sntp_stop_calls(void) {
    return host_sntp_stop_calls;
}

int time_sync_host_sntp_restart_calls(void) {
    return host_sntp_restart_calls;
}

const char *time_sync_host_sntp_server(void) {
    return host_sntp_server;
}

esp_sntp_operatingmode_t time_sync_host_sntp_mode(void) {
    return host_sntp_mode;
}

sntp_sync_status_t time_sync_host_sntp_status(void) {
    return host_sntp_status;
}

const char *time_sync_host_string(const char *key) {
    const host_value_t *entry = host_find_value(key);
    return entry != NULL ? entry->value : "";
}

int time_sync_host_set_string_calls(const char *key) {
    const host_value_t *entry = host_find_value(key);
    return entry != NULL ? entry->set_calls : 0;
}

bool config_store_is_ready(void) {
    return host_store_ready;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    const host_value_t *entry = host_find_value(key);
    if (entry == NULL) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (output == NULL || output_size < strlen(entry->value) + 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, entry->value);
    return ESP_OK;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    host_value_t *entry = host_add_value(key);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }
    entry->set_calls += 1;
    if (entry->set_result != ESP_OK) {
        return entry->set_result;
    }
    strncpy(
        entry->value,
        value != NULL ? value : "",
        sizeof(entry->value) - 1
    );
    entry->value[sizeof(entry->value) - 1] = '\0';
    return ESP_OK;
}

int settimeofday(const struct timeval *value, const struct timezone *timezone) {
    (void)timezone;
    host_clock_calls += 1;
    if (host_clock_result != 0 || value == NULL) {
        return -1;
    }
    host_clock_epoch = (int64_t)value->tv_sec;
    return 0;
}

network_manager_state_t network_manager_get_state(void) {
    return host_network_state;
}

esp_err_t network_manager_add_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    if (host_callback_result != ESP_OK) {
        return host_callback_result;
    }
    host_network_callback = callback;
    host_network_context = context;
    return ESP_OK;
}

esp_err_t network_manager_remove_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    if (host_network_callback == callback &&
        host_network_context == context) {
        host_network_callback = NULL;
        host_network_context = NULL;
    }
    return ESP_OK;
}

void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t operating_mode) {
    host_sntp_mode = operating_mode;
}

void esp_sntp_setservername(uint8_t index, const char *server) {
    (void)index;
    strncpy(
        host_sntp_server,
        server != NULL ? server : "",
        sizeof(host_sntp_server) - 1
    );
    host_sntp_server[sizeof(host_sntp_server) - 1] = '\0';
}

void esp_sntp_set_time_sync_notification_cb(
    esp_sntp_time_sync_notification_cb_t callback
) {
    host_sntp_callback = callback;
}

void esp_sntp_set_sync_status(sntp_sync_status_t status) {
    host_sntp_status = status;
}

sntp_sync_status_t esp_sntp_get_sync_status(void) {
    return host_sntp_status;
}

void esp_sntp_init(void) {
    host_sntp_init_calls += 1;
}

void esp_sntp_stop(void) {
    host_sntp_stop_calls += 1;
}

void esp_sntp_restart(void) {
    host_sntp_restart_calls += 1;
    host_sntp_status = SNTP_SYNC_STATUS_IN_PROGRESS;
}

const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}
