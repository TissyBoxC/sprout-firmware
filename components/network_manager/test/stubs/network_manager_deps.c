#include "config_store.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/semphr.h"

#include <string.h>

// Host stand-ins for Wi-Fi, the default event loop, the station netif, and the
// configuration store. The test can emit both Wi-Fi and IP events directly so
// every station transition is deterministic.
static bool host_create_mutex_fails;
static bool host_config_store_ready = true;
static esp_err_t host_netif_init_result = ESP_OK;
static esp_err_t host_event_loop_result = ESP_OK;
static bool host_create_station_netif_returns_null;
static esp_err_t host_wifi_init_result = ESP_OK;
static esp_err_t host_register_result = ESP_OK;
static esp_err_t host_set_storage_result = ESP_OK;
static esp_err_t host_set_mode_result = ESP_OK;
static esp_err_t host_wifi_start_result = ESP_OK;
static esp_err_t host_connect_result = ESP_OK;
static esp_err_t host_disconnect_result = ESP_OK;
static esp_err_t host_set_config_result = ESP_OK;
static esp_err_t host_get_ap_info_result = ESP_OK;
static esp_err_t host_store_set_result = ESP_OK;
static esp_err_t host_store_get_result = ESP_OK;

static int host_mutex;
static int host_netif_station;
static esp_netif_t *host_station_netif;
static char host_stored_ssid[CONFIG_STORE_SSID_SIZE + 1];
static char host_stored_password[CONFIG_STORE_PASSWORD_SIZE + 1];
static char host_ap_ssid[WIFI_SSID_MAX_LEN];
static size_t host_ap_ssid_length;

static int host_netif_init_calls;
static int host_create_netif_calls;
static int host_event_loop_calls;
static int host_register_calls;
static int host_wifi_init_calls;
static int host_wifi_start_calls;
static int host_connect_calls;
static int host_disconnect_calls;
static int host_set_config_calls;
static int host_store_set_calls;
static int host_store_get_calls;
static wifi_config_t host_last_wifi_config;
static wifi_mode_t host_last_mode;
static wifi_storage_t host_last_storage;

static esp_event_handler_t host_wifi_handler;
static esp_event_handler_t host_ip_handler;

const char *WIFI_EVENT = "WIFI_EVENT";
const char *IP_EVENT = "IP_EVENT";

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return host_create_mutex_fails ? NULL : &host_mutex;
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer) {
    (void)buffer;
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

bool config_store_is_ready(void) {
    return host_config_store_ready;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    ++host_store_set_calls;
    if (host_store_set_result != ESP_OK) {
        return host_store_set_result;
    }
    if (key != NULL && strcmp(key, "wifi_ssid") == 0) {
        strncpy(host_stored_ssid, value != NULL ? value : "", sizeof(host_stored_ssid) - 1);
    }
    if (key != NULL && strcmp(key, "wifi_password") == 0) {
        strncpy(
            host_stored_password,
            value != NULL ? value : "",
            sizeof(host_stored_password) - 1
        );
    }
    return ESP_OK;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    ++host_store_get_calls;
    if (host_store_get_result != ESP_OK) {
        return host_store_get_result;
    }
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *source = "";
    if (key != NULL && strcmp(key, "wifi_ssid") == 0) {
        source = host_stored_ssid;
    } else if (key != NULL && strcmp(key, "wifi_password") == 0) {
        source = host_stored_password;
    }
    strncpy(output, source, output_size - 1);
    output[output_size - 1] = '\0';
    return ESP_OK;
}

esp_err_t esp_netif_init(void) {
    ++host_netif_init_calls;
    return host_netif_init_result;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void) {
    ++host_create_netif_calls;
    if (host_create_station_netif_returns_null) {
        return NULL;
    }
    host_station_netif = (esp_netif_t *)&host_netif_station;
    return host_station_netif;
}

esp_err_t esp_event_loop_create_default(void) {
    ++host_event_loop_calls;
    return host_event_loop_result;
}

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler,
    void *handler_arg,
    void **instance
) {
    (void)event_id;
    (void)handler_arg;
    (void)instance;
    ++host_register_calls;
    if (host_register_result != ESP_OK) {
        return host_register_result;
    }
    if (event_base == WIFI_EVENT) {
        host_wifi_handler = handler;
    } else if (event_base == IP_EVENT) {
        host_ip_handler = handler;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    (void)config;
    ++host_wifi_init_calls;
    return host_wifi_init_result;
}

esp_err_t esp_wifi_set_storage(wifi_storage_t storage) {
    host_last_storage = storage;
    return host_set_storage_result;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    host_last_mode = mode;
    return host_set_mode_result;
}

esp_err_t esp_wifi_start(void) {
    ++host_wifi_start_calls;
    return host_wifi_start_result;
}

esp_err_t esp_wifi_connect(void) {
    ++host_connect_calls;
    return host_connect_result;
}

esp_err_t esp_wifi_disconnect(void) {
    ++host_disconnect_calls;
    return host_disconnect_result;
}

esp_err_t esp_wifi_set_config(
    wifi_interface_t interface,
    const wifi_config_t *config
) {
    (void)interface;
    ++host_set_config_calls;
    if (config != NULL) {
        host_last_wifi_config = *config;
    }
    return host_set_config_result;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *access_point_record) {
    if (host_get_ap_info_result != ESP_OK) {
        return host_get_ap_info_result;
    }
    if (access_point_record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(access_point_record->ssid, 0, sizeof(access_point_record->ssid));
    memcpy(access_point_record->ssid, host_ap_ssid, host_ap_ssid_length);
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}

// Test hooks.
void network_manager_host_reset(void) {
    host_create_mutex_fails = false;
    host_config_store_ready = true;
    host_netif_init_result = ESP_OK;
    host_event_loop_result = ESP_OK;
    host_create_station_netif_returns_null = false;
    host_wifi_init_result = ESP_OK;
    host_register_result = ESP_OK;
    host_set_storage_result = ESP_OK;
    host_set_mode_result = ESP_OK;
    host_wifi_start_result = ESP_OK;
    host_connect_result = ESP_OK;
    host_disconnect_result = ESP_OK;
    host_set_config_result = ESP_OK;
    host_get_ap_info_result = ESP_OK;
    host_store_set_result = ESP_OK;
    host_store_get_result = ESP_OK;
    host_netif_init_calls = 0;
    host_create_netif_calls = 0;
    host_event_loop_calls = 0;
    host_register_calls = 0;
    host_wifi_init_calls = 0;
    host_wifi_start_calls = 0;
    host_connect_calls = 0;
    host_disconnect_calls = 0;
    host_set_config_calls = 0;
    host_store_set_calls = 0;
    host_store_get_calls = 0;
    host_ap_ssid_length = 0;
    host_last_mode = WIFI_MODE_NULL;
    host_last_storage = WIFI_STORAGE_FLASH;
    memset(host_stored_ssid, 0, sizeof(host_stored_ssid));
    memset(host_stored_password, 0, sizeof(host_stored_password));
    memset(host_ap_ssid, 0, sizeof(host_ap_ssid));
    memset(&host_last_wifi_config, 0, sizeof(host_last_wifi_config));
    /* The module registers its event handlers exactly once for the process, so
       a per-case reset must not detach them. */
}

void network_manager_host_set_create_mutex_fails(bool fails) {
    host_create_mutex_fails = fails;
}

void network_manager_host_set_config_store_ready(bool ready) {
    host_config_store_ready = ready;
}

void network_manager_host_set_netif_init_result(esp_err_t result) {
    host_netif_init_result = result;
}

void network_manager_host_set_event_loop_result(esp_err_t result) {
    host_event_loop_result = result;
}

void network_manager_host_set_create_netif_null(bool returns_null) {
    host_create_station_netif_returns_null = returns_null;
}

void network_manager_host_set_wifi_init_result(esp_err_t result) {
    host_wifi_init_result = result;
}

void network_manager_host_set_register_result(esp_err_t result) {
    host_register_result = result;
}

void network_manager_host_set_set_mode_result(esp_err_t result) {
    host_set_mode_result = result;
}

void network_manager_host_set_wifi_start_result(esp_err_t result) {
    host_wifi_start_result = result;
}

void network_manager_host_set_connect_result(esp_err_t result) {
    host_connect_result = result;
}

void network_manager_host_set_disconnect_result(esp_err_t result) {
    host_disconnect_result = result;
}

void network_manager_host_set_set_config_result(esp_err_t result) {
    host_set_config_result = result;
}

void network_manager_host_set_get_ap_info_result(esp_err_t result) {
    host_get_ap_info_result = result;
}

void network_manager_host_set_store_set_result(esp_err_t result) {
    host_store_set_result = result;
}

void network_manager_host_set_store_get_result(esp_err_t result) {
    host_store_get_result = result;
}

void network_manager_host_set_ap_ssid(const char *ssid) {
    host_ap_ssid_length = 0;
    memset(host_ap_ssid, 0, sizeof(host_ap_ssid));
    if (ssid == NULL) {
        return;
    }
    host_ap_ssid_length = strlen(ssid);
    if (host_ap_ssid_length > sizeof(host_ap_ssid)) {
        host_ap_ssid_length = sizeof(host_ap_ssid);
    }
    memcpy(host_ap_ssid, ssid, host_ap_ssid_length);
}

void network_manager_host_emit_wifi_event(int32_t event_id) {
    if (host_wifi_handler != NULL) {
        host_wifi_handler(NULL, WIFI_EVENT, event_id, NULL);
    }
}

void network_manager_host_emit_ip_event(void) {
    if (host_ip_handler != NULL) {
        host_ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    }
}

int network_manager_host_netif_init_calls(void) {
    return host_netif_init_calls;
}

int network_manager_host_create_netif_calls(void) {
    return host_create_netif_calls;
}

int network_manager_host_wifi_init_calls(void) {
    return host_wifi_init_calls;
}

int network_manager_host_wifi_start_calls(void) {
    return host_wifi_start_calls;
}

int network_manager_host_connect_calls(void) {
    return host_connect_calls;
}

int network_manager_host_disconnect_calls(void) {
    return host_disconnect_calls;
}

int network_manager_host_set_config_calls(void) {
    return host_set_config_calls;
}

int network_manager_host_store_set_calls(void) {
    return host_store_set_calls;
}

int network_manager_host_store_get_calls(void) {
    return host_store_get_calls;
}

wifi_config_t network_manager_host_last_wifi_config(void) {
    return host_last_wifi_config;
}

wifi_mode_t network_manager_host_last_mode(void) {
    return host_last_mode;
}

wifi_storage_t network_manager_host_last_storage(void) {
    return host_last_storage;
}
