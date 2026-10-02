#include "offline_fallback.h"

#include "esp_timer.h"
#include "network_manager.h"

#if CONFIG_FEATURE_TIME_SYNC
#include "time_sync.h"
#endif

#define OFFLINE_FALLBACK_MAXIMUM_PENDING_TELEMETRY 10000

static bool offline_fallback_ready;
static esp_timer_handle_t offline_fallback_timer;
static offline_fallback_snapshot_t offline_fallback_snapshot = {
    .state = OFFLINE_FALLBACK_STATE_ONLINE,
    .reason = OFFLINE_REASON_NONE,
    .fallback_active = false,
    .pending_telemetry = 0,
};

static void offline_fallback_set_state(
    offline_fallback_state_t state,
    offline_reason_t reason,
    bool fallback_active
) {
    offline_fallback_snapshot.state = state;
    offline_fallback_snapshot.reason = reason;
    offline_fallback_snapshot.fallback_active = fallback_active;
}

static void offline_fallback_timer_callback(void *argument) {
    (void)argument;
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        offline_fallback_set_state(
            OFFLINE_FALLBACK_STATE_OFFLINE,
            OFFLINE_REASON_NETWORK_UNAVAILABLE,
            true
        );
    }
}

static void offline_fallback_network_callback(
    network_manager_state_t state,
    void *context
) {
    (void)context;
    if (state == NETWORK_MANAGER_STATE_CONNECTED) {
        offline_fallback_mark_recovered();
        return;
    }
    if (offline_fallback_timer != NULL) {
        esp_timer_stop(offline_fallback_timer);
    }
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_GRACE,
        OFFLINE_REASON_NETWORK_UNAVAILABLE,
        false
    );
    if (offline_fallback_timer != NULL &&
        CONFIG_OFFLINE_FALLBACK_GRACE_SECONDS > 0) {
        esp_timer_start_once(
            offline_fallback_timer,
            (uint64_t)CONFIG_OFFLINE_FALLBACK_GRACE_SECONDS * 1000000ULL
        );
    }
}

esp_err_t offline_fallback_init(void) {
    if (offline_fallback_ready) {
        return ESP_OK;
    }
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
        esp_timer_delete(offline_fallback_timer);
        offline_fallback_timer = NULL;
        return result;
    }
    offline_fallback_ready = true;
    return ESP_OK;
}

offline_fallback_snapshot_t offline_fallback_get_snapshot(void) {
    return offline_fallback_snapshot;
}

esp_err_t offline_fallback_record_pending_telemetry(void) {
    if (offline_fallback_snapshot.pending_telemetry <
        OFFLINE_FALLBACK_MAXIMUM_PENDING_TELEMETRY) {
        ++offline_fallback_snapshot.pending_telemetry;
    }
    return ESP_OK;
}

esp_err_t offline_fallback_clear_pending_telemetry(void) {
    offline_fallback_snapshot.pending_telemetry = 0;
    return ESP_OK;
}

esp_err_t offline_fallback_mark_service_unavailable(void) {
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_GRACE,
        OFFLINE_REASON_SERVICE_UNAVAILABLE,
        false
    );
    return ESP_OK;
}

esp_err_t offline_fallback_mark_time_unsynchronized(void) {
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_GRACE,
        OFFLINE_REASON_TIME_NOT_SYNCHRONIZED,
        false
    );
    return ESP_OK;
}

esp_err_t offline_fallback_mark_recovered(void) {
#if CONFIG_FEATURE_TIME_SYNC
    if (!time_sync_is_synchronized()) {
        return offline_fallback_mark_time_unsynchronized();
    }
#endif
    offline_fallback_clear_pending_telemetry();
    offline_fallback_set_state(
        OFFLINE_FALLBACK_STATE_ONLINE,
        OFFLINE_REASON_NONE,
        false
    );
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
