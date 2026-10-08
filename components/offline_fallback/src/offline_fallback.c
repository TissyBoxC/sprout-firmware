#include "offline_fallback.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_store.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "network_manager.h"

#if CONFIG_FEATURE_TIME_SYNC
#include "time_sync.h"
#endif

#define OFFLINE_FALLBACK_MAXIMUM_PENDING_TELEMETRY 10000
#define OFFLINE_FALLBACK_PENDING_KEY "offline_pending"
#define OFFLINE_FALLBACK_LOST_KEY "offline_lost"

static bool offline_fallback_ready;
static esp_timer_handle_t offline_fallback_timer;
static SemaphoreHandle_t offline_fallback_mutex;
static bool offline_fallback_network_lost;
static bool offline_fallback_network_reconnected_pending;
static offline_fallback_snapshot_t offline_fallback_snapshot = {
    .state = OFFLINE_FALLBACK_STATE_ONLINE,
    .reason = OFFLINE_REASON_NONE,
    .fallback_active = false,
    .pending_telemetry = 0,
};

static bool offline_fallback_lock(void) {
    return offline_fallback_mutex != NULL &&
        xSemaphoreTake(offline_fallback_mutex, portMAX_DELAY) == pdTRUE;
}

static void offline_fallback_unlock(void) {
    if (offline_fallback_mutex != NULL) {
        (void)xSemaphoreGive(offline_fallback_mutex);
    }
}

static void offline_fallback_set_state(
    offline_fallback_state_t state,
    offline_reason_t reason,
    bool fallback_active
) {
    offline_fallback_snapshot.state = state;
    offline_fallback_snapshot.reason = reason;
    offline_fallback_snapshot.fallback_active = fallback_active;
}

static esp_err_t offline_fallback_persist_pending_locked(void) {
    char pending_text[16] = {0};
    const int written = snprintf(
        pending_text,
        sizeof(pending_text),
        "%u",
        (unsigned)offline_fallback_snapshot.pending_telemetry
    );
    if (written <= 0 || (size_t)written >= sizeof(pending_text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return config_store_set_string(
        OFFLINE_FALLBACK_PENDING_KEY,
        pending_text
    );
}

static void offline_fallback_load_pending(void) {
    char pending_text[16] = {0};
    const esp_err_t result = config_store_get_string(
        OFFLINE_FALLBACK_PENDING_KEY,
        pending_text,
        sizeof(pending_text)
    );
    if (result != ESP_OK) {
        return;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(pending_text, &end, 10);
    if (end == pending_text || *end != '\0' ||
        parsed > OFFLINE_FALLBACK_MAXIMUM_PENDING_TELEMETRY) {
        return;
    }
    offline_fallback_snapshot.pending_telemetry = (uint16_t)parsed;
}

static void offline_fallback_persist_lost(bool is_lost) {
    (void)config_store_set_string(
        OFFLINE_FALLBACK_LOST_KEY,
        is_lost ? "1" : "0"
    );
}

static void offline_fallback_load_lost(void) {
    char lost_text[4] = {0};
    if (config_store_get_string(
            OFFLINE_FALLBACK_LOST_KEY,
            lost_text,
            sizeof(lost_text)) == ESP_OK) {
        offline_fallback_network_lost = strcmp(lost_text, "1") == 0;
    }
}

static void offline_fallback_timer_callback(void *argument) {
    (void)argument;
    if (!offline_fallback_lock()) {
        return;
    }
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        offline_fallback_set_state(
            OFFLINE_FALLBACK_STATE_OFFLINE,
            OFFLINE_REASON_NETWORK_UNAVAILABLE,
            true
        );
    }
    offline_fallback_unlock();
}

static void offline_fallback_network_callback(
    network_manager_state_t state,
    void *context
) {
    (void)context;
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return;
    }
    if (state == NETWORK_MANAGER_STATE_CONNECTED) {
        offline_fallback_set_state(
            OFFLINE_FALLBACK_STATE_GRACE,
            OFFLINE_REASON_SERVICE_UNAVAILABLE,
            false
        );
        if (offline_fallback_network_lost) {
            offline_fallback_network_lost = false;
            offline_fallback_network_reconnected_pending = true;
            offline_fallback_persist_lost(false);
        }
        offline_fallback_unlock();
        return;
    }

    if (!offline_fallback_network_lost) {
        offline_fallback_network_lost = true;
        offline_fallback_persist_lost(true);
    }
    if (offline_fallback_timer != NULL) {
        (void)esp_timer_stop(offline_fallback_timer);
    }
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_GRACE,
        OFFLINE_REASON_NETWORK_UNAVAILABLE,
        false
    );
    if (offline_fallback_timer != NULL &&
        CONFIG_OFFLINE_FALLBACK_GRACE_SECONDS > 0) {
        (void)esp_timer_start_once(
            offline_fallback_timer,
            (uint64_t)CONFIG_OFFLINE_FALLBACK_GRACE_SECONDS * 1000000ULL
        );
    }
    offline_fallback_unlock();
}

esp_err_t offline_fallback_init(void) {
    if (offline_fallback_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (offline_fallback_mutex == NULL) {
        offline_fallback_mutex = xSemaphoreCreateMutex();
        if (offline_fallback_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!offline_fallback_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    offline_fallback_load_pending();
    offline_fallback_load_lost();
    offline_fallback_unlock();

    const esp_timer_create_args_t timer_args = {
        .callback = offline_fallback_timer_callback,
        .name = "sprout_offline",
    };
    esp_err_t result = esp_timer_create(
        &timer_args,
        &offline_fallback_timer
    );
    if (result != ESP_OK) {
        return result;
    }
    result = network_manager_add_state_callback(
        offline_fallback_network_callback,
        NULL
    );
    if (result != ESP_OK) {
        (void)esp_timer_delete(offline_fallback_timer);
        offline_fallback_timer = NULL;
        return result;
    }
    offline_fallback_ready = true;
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        if (offline_fallback_lock()) {
            if (offline_fallback_network_lost) {
                offline_fallback_set_state(
                    OFFLINE_FALLBACK_STATE_GRACE,
                    OFFLINE_REASON_NETWORK_UNAVAILABLE,
                    false
                );
            }
            // A reboot during a network outage loses the original disconnect
            // callback, so restart the grace countdown from the persisted
            // loss flag instead of leaving the device in grace forever.
            if (CONFIG_OFFLINE_FALLBACK_GRACE_SECONDS > 0) {
                (void)esp_timer_stop(offline_fallback_timer);
                (void)esp_timer_start_once(
                    offline_fallback_timer,
                    (uint64_t)CONFIG_OFFLINE_FALLBACK_GRACE_SECONDS * 1000000ULL
                );
            }
            offline_fallback_unlock();
        }
    }
    return ESP_OK;
}

offline_fallback_snapshot_t offline_fallback_get_snapshot(void) {
    offline_fallback_snapshot_t snapshot = {
        .state = OFFLINE_FALLBACK_STATE_ONLINE,
        .reason = OFFLINE_REASON_NONE,
        .fallback_active = false,
        .pending_telemetry = 0,
    };
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return snapshot;
    }
    snapshot = offline_fallback_snapshot;
    offline_fallback_unlock();
    return snapshot;
}

esp_err_t offline_fallback_record_pending_telemetry(void) {
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (offline_fallback_snapshot.pending_telemetry >=
        OFFLINE_FALLBACK_MAXIMUM_PENDING_TELEMETRY) {
        offline_fallback_unlock();
        return ESP_OK;
    }

    const uint16_t previous = offline_fallback_snapshot.pending_telemetry;
    ++offline_fallback_snapshot.pending_telemetry;
    const esp_err_t result = offline_fallback_persist_pending_locked();
    if (result != ESP_OK) {
        offline_fallback_snapshot.pending_telemetry = previous;
    }
    offline_fallback_unlock();
    return result;
}

esp_err_t offline_fallback_clear_pending_telemetry(void) {
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t previous = offline_fallback_snapshot.pending_telemetry;
    offline_fallback_snapshot.pending_telemetry = 0;
    const esp_err_t result = offline_fallback_persist_pending_locked();
    if (result != ESP_OK) {
        offline_fallback_snapshot.pending_telemetry = previous;
    }
    offline_fallback_unlock();
    return result;
}

bool offline_fallback_consume_network_reconnected(void) {
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return false;
    }
    const bool reconnected = offline_fallback_network_reconnected_pending;
    offline_fallback_network_reconnected_pending = false;
    offline_fallback_unlock();
    return reconnected;
}

esp_err_t offline_fallback_mark_service_unavailable(void) {
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_GRACE,
        OFFLINE_REASON_SERVICE_UNAVAILABLE,
        false
    );
    offline_fallback_unlock();
    return ESP_OK;
}

esp_err_t offline_fallback_mark_time_unsynchronized(void) {
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_GRACE,
        OFFLINE_REASON_TIME_NOT_SYNCHRONIZED,
        false
    );
    offline_fallback_unlock();
    return ESP_OK;
}

esp_err_t offline_fallback_mark_recovered(void) {
    if (!offline_fallback_ready || !offline_fallback_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_FEATURE_TIME_SYNC
    if (!time_sync_is_synchronized()) {
        offline_fallback_set_state(
            OFFLINE_FALLBACK_STATE_GRACE,
            OFFLINE_REASON_TIME_NOT_SYNCHRONIZED,
            false
        );
        offline_fallback_unlock();
        return ESP_OK;
    }
#endif
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_ONLINE,
        OFFLINE_REASON_NONE,
        false
    );
    offline_fallback_unlock();
    return ESP_OK;
}

const module_descriptor_t *offline_fallback_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "offline_fallback",
        .version = "1.0.0",
        .initialize = offline_fallback_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
