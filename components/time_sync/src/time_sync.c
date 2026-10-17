#include "time_sync.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "network_manager.h"
#include "sdkconfig.h"

#ifndef ESP_PLATFORM
int settimeofday(const struct timeval *value, const struct timezone *timezone);
#endif

#define TIME_SYNC_LAST_SYNCED_KEY "time_synced_at"
#define TIME_SYNC_OFFSET_KEY "time_offset_ms"
#define TIME_SYNC_TRUSTED_KEY "time_trusted"
CONFIG_STORE_KEY_ASSERT(TIME_SYNC_LAST_SYNCED_KEY);
CONFIG_STORE_KEY_ASSERT(TIME_SYNC_OFFSET_KEY);
CONFIG_STORE_KEY_ASSERT(TIME_SYNC_TRUSTED_KEY);
#define TIME_SYNC_MINIMUM_EPOCH 1704067200  /* 2024-01-01T00:00:00Z */
#define TIME_SYNC_SECONDS_PER_DAY 86400

#ifndef CONFIG_TIME_SYNC_TIMEZONE_OFFSET_MINUTES
#define CONFIG_TIME_SYNC_TIMEZONE_OFFSET_MINUTES 480
#endif

static const char *const TAG = "time_sync";

static bool time_sync_ready;
static time_sync_state_t time_sync_state = TIME_SYNC_STATE_UNSYNCHRONIZED;
static time_sync_source_t time_sync_source = TIME_SYNC_SOURCE_NONE;
static int64_t time_sync_last_synced_epoch;
static int time_sync_offset_ms;
static time_sync_state_callback_t time_sync_callback;
static void *time_sync_callback_context;

static void time_sync_network_state_callback(
    network_manager_state_t state,
    void *context
);

#ifdef TIME_SYNC_HOST_TESTS
void time_sync_host_reset_module(void) {
    if (time_sync_ready) {
        (void)network_manager_remove_state_callback(
            time_sync_network_state_callback,
            NULL
        );
        esp_sntp_stop();
    }
    time_sync_ready = false;
    time_sync_state = TIME_SYNC_STATE_UNSYNCHRONIZED;
    time_sync_source = TIME_SYNC_SOURCE_NONE;
    time_sync_last_synced_epoch = 0;
    time_sync_offset_ms = 0;
    time_sync_callback = NULL;
    time_sync_callback_context = NULL;
}
#endif

static void time_sync_publish_state(void) {
    if (time_sync_callback != NULL) {
        time_sync_callback(
            time_sync_state,
            time_sync_source,
            time_sync_callback_context
        );
    }
}

static esp_err_t time_sync_persist_trusted_time(void) {
    char epoch_text[32] = {0};
    const int written = snprintf(
        epoch_text,
        sizeof(epoch_text),
        "%lld",
        (long long)time_sync_last_synced_epoch
    );
    if (written <= 0 || (size_t)written >= sizeof(epoch_text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t epoch_result = config_store_set_string(
        TIME_SYNC_LAST_SYNCED_KEY,
        epoch_text
    );
    if (epoch_result != ESP_OK) {
        return epoch_result;
    }
    return config_store_set_string(TIME_SYNC_TRUSTED_KEY, "1");
}

static esp_err_t time_sync_persist_offset(void) {
    char offset_text[16] = {0};
    const int written = snprintf(offset_text, sizeof(offset_text), "%d", time_sync_offset_ms);
    if (written <= 0 || (size_t)written >= sizeof(offset_text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return config_store_set_string(TIME_SYNC_OFFSET_KEY, offset_text);
}

static esp_err_t time_sync_persist_state(void) {
    esp_err_t result = time_sync_persist_trusted_time();
    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "trusted time persistence failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    result = time_sync_persist_offset();
    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "time offset persistence failed: %s",
            esp_err_to_name(result)
        );
    }
    return result;
}

static esp_err_t time_sync_apply_clock(int64_t epoch) {
    struct timeval value = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&value, NULL) != 0) {
        ESP_LOGW(TAG, "system clock update failed");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t time_sync_mark_synchronized(
    int64_t epoch,
    time_sync_source_t source,
    int offset_ms,
    bool clock_already_updated
) {
    if (!clock_already_updated) {
        const esp_err_t clock_result = time_sync_apply_clock(epoch);
        if (clock_result != ESP_OK) {
            return clock_result;
        }
    }

    time_sync_last_synced_epoch = epoch;
    time_sync_source = source;
    time_sync_offset_ms = offset_ms;
    time_sync_state = TIME_SYNC_STATE_SYNCHRONIZED;

    const esp_err_t persist_result = time_sync_persist_state();
    time_sync_publish_state();
    return persist_result;
}

static void time_sync_load_trusted_state(void) {
    char trusted[4] = {0};
    if (config_store_get_string(
            TIME_SYNC_TRUSTED_KEY,
            trusted,
            sizeof(trusted)) != ESP_OK ||
        strcmp(trusted, "1") != 0) {
        return;
    }
    char epoch_text[32] = {0};
    if (config_store_get_string(
            TIME_SYNC_LAST_SYNCED_KEY,
            epoch_text,
            sizeof(epoch_text)) != ESP_OK) {
        return;
    }
    const long long parsed = atoll(epoch_text);
    if (parsed < TIME_SYNC_MINIMUM_EPOCH) {
        return;
    }
    time_sync_last_synced_epoch = (int64_t)parsed;
    char offset_text[16] = {0};
    if (config_store_get_string(
            TIME_SYNC_OFFSET_KEY,
            offset_text,
            sizeof(offset_text)) == ESP_OK) {
        time_sync_offset_ms = atoi(offset_text);
    }

    // Restore the last trusted clock before Wi-Fi is available so TLS and
    // certificate validation do not depend on an obviously invalid epoch.
    struct timeval restored_time = {
        .tv_sec = (time_t)time_sync_last_synced_epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&restored_time, NULL) != 0) {
        ESP_LOGW(TAG, "restoring persisted time failed");
        time_sync_last_synced_epoch = 0;
        time_sync_offset_ms = 0;
    }
}

static void time_sync_sntp_callback(struct timeval *time_value) {
    if (time_value == NULL) {
        return;
    }
    const int64_t epoch = (int64_t)time_value->tv_sec;
    if (epoch < TIME_SYNC_MINIMUM_EPOCH) {
        return;
    }
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    (void)time_sync_mark_synchronized(
        epoch,
        TIME_SYNC_SOURCE_SNTP,
        time_sync_offset_ms,
        true
    );
}

static void time_sync_network_state_callback(
    network_manager_state_t state,
    void *context
) {
    (void)context;
    if (state != NETWORK_MANAGER_STATE_CONNECTED) {
        return;
    }
    if (!time_sync_is_synchronized()) {
        time_sync_state = TIME_SYNC_STATE_SYNCHRONIZING;
        time_sync_source = TIME_SYNC_SOURCE_SNTP;
        time_sync_publish_state();
        esp_sntp_restart();
    }
}

esp_err_t time_sync_init(void) {
    if (time_sync_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    time_sync_load_trusted_state();
    if (time_sync_last_synced_epoch >= TIME_SYNC_MINIMUM_EPOCH) {
        time_sync_state = TIME_SYNC_STATE_SYNCHRONIZED;
        time_sync_source = TIME_SYNC_SOURCE_PLATFORM;
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_TIME_SYNC_SNTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(time_sync_sntp_callback);
    esp_sntp_init();

    const esp_err_t callback_result = network_manager_add_state_callback(
        time_sync_network_state_callback,
        NULL
    );
    if (callback_result != ESP_OK) {
        esp_sntp_stop();
        return callback_result;
    }
    time_sync_ready = true;
    return ESP_OK;
}

time_sync_state_t time_sync_get_state(void) {
    return time_sync_state;
}

bool time_sync_is_ready(void) {
    return time_sync_ready;
}

time_sync_source_t time_sync_get_source(void) {
    return time_sync_source;
}

int64_t time_sync_get_last_synced_epoch(void) {
    return time_sync_last_synced_epoch;
}

int time_sync_get_offset_ms(void) {
    if (time_sync_offset_ms < -60000) {
        return -60000;
    }
    if (time_sync_offset_ms > 60000) {
        return 60000;
    }
    return time_sync_offset_ms;
}

bool time_sync_is_synchronized(void) {
    return time_sync_state == TIME_SYNC_STATE_SYNCHRONIZED;
}

int32_t time_sync_get_timezone_offset_minutes(void) {
    return CONFIG_TIME_SYNC_TIMEZONE_OFFSET_MINUTES;
}

esp_err_t time_sync_resynchronize(void) {
    if (!time_sync_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    time_sync_state = TIME_SYNC_STATE_SYNCHRONIZING;
    time_sync_source = TIME_SYNC_SOURCE_SNTP;
    time_sync_publish_state();
    esp_sntp_restart();
    return ESP_OK;
}

esp_err_t time_sync_accept_platform_time(int64_t unix_time_seconds) {
    if (!time_sync_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (unix_time_seconds < TIME_SYNC_MINIMUM_EPOCH) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = time_sync_mark_synchronized(
        unix_time_seconds,
        TIME_SYNC_SOURCE_PLATFORM,
        0,
        false
    );
    return result == ESP_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t time_sync_set_state_callback(
    time_sync_state_callback_t callback,
    void *context
) {
    time_sync_callback = callback;
    time_sync_callback_context = context;
    return ESP_OK;
}

const module_descriptor_t *time_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "time_sync",
        .version = "1.0.0",
        .initialize = time_sync_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
