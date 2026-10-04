#include "factory_reset.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER && __has_include("diagnostic_reporter.h")
#include "diagnostic_reporter.h"
#define FACTORY_RESET_HAS_DIAGNOSTIC_REPORTER 1
#else
#define FACTORY_RESET_HAS_DIAGNOSTIC_REPORTER 0
#endif

#if CONFIG_FEATURE_CONFIG_STORE && __has_include("config_store.h")
#include "config_store.h"
#define FACTORY_RESET_HAS_CONFIG_STORE 1
#else
#define FACTORY_RESET_HAS_CONFIG_STORE 0
#endif

static const char *const TAG = "factory_reset";

#define FACTORY_RESET_TIMEOUT_MS \
    (CONFIG_FACTORY_RESET_PENDING_TIMEOUT_SECONDS * 1000)

#if FACTORY_RESET_HAS_CONFIG_STORE
#define FACTORY_RESET_CONFIG_KEY_WIFI_SSID "wifi_ssid"
#define FACTORY_RESET_CONFIG_KEY_WIFI_PASSWORD "wifi_password"
#define FACTORY_RESET_CONFIG_KEY_PROVISIONING_SALT "provisioning_srp_salt"
#define FACTORY_RESET_CONFIG_KEY_PROVISIONING_VERIFIER "provisioning_srp_verifier"
#define FACTORY_RESET_CONFIG_KEY_PROVISIONING_POP "provisioning_pop"
#define FACTORY_RESET_CONFIG_KEY_PLATFORM_URL "platform_url"
#define FACTORY_RESET_CONFIG_KEY_DEVICE_BOUND "device_bound"
#define FACTORY_RESET_CONFIG_KEY_DEVICE_REGISTERED "dev_registered"
#define FACTORY_RESET_CONFIG_KEY_DEVICE_SESSION "device_session"
#define FACTORY_RESET_CONFIG_KEY_DEVICE_REGISTRATION_TOKEN "dev_reg_token"
#endif

static SemaphoreHandle_t factory_reset_mutex;
static esp_timer_handle_t factory_reset_timer;
static bool factory_reset_ready;
static bool factory_reset_pending;
static factory_reset_reason_t factory_reset_pending_reason;
static int64_t factory_reset_requested_at_ms;
static factory_reset_error_t factory_reset_last_result;
static uint32_t factory_reset_completed_count;

static void factory_reset_report_event(
    const char *event_type,
    const char *detail_code
) {
#if FACTORY_RESET_HAS_DIAGNOSTIC_REPORTER
    // Reset events are instantaneous state transitions. Keep duration zero
    // and use stable reason/error names as the detail code.
    (void)diagnostic_reporter_record_interaction(
        event_type,
        detail_code,
        0
    );
#else
    (void)event_type;
    (void)detail_code;
#endif
}

static bool factory_reset_reason_is_valid(factory_reset_reason_t reason) {
    return reason >= FACTORY_RESET_REASON_GUARDIAN_REQUEST &&
        reason <= FACTORY_RESET_REASON_INTERNAL_RECOVERY;
}

static int64_t factory_reset_now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static bool factory_reset_pending_is_expired_locked(void) {
    if (!factory_reset_pending) {
        return false;
    }
    return factory_reset_now_ms() - factory_reset_requested_at_ms >=
        FACTORY_RESET_TIMEOUT_MS;
}

static void factory_reset_expire_locked(void) {
    factory_reset_pending = false;
    factory_reset_last_result = FACTORY_RESET_ERR_EXPIRED;
}

static uint32_t factory_reset_remaining_ms_locked(void) {
    if (!factory_reset_pending) {
        return 0;
    }
    const int64_t elapsed_ms =
        factory_reset_now_ms() - factory_reset_requested_at_ms;
    if (elapsed_ms <= 0) {
        return FACTORY_RESET_TIMEOUT_MS;
    }
    if (elapsed_ms >= FACTORY_RESET_TIMEOUT_MS) {
        return 0;
    }
    return (uint32_t)(FACTORY_RESET_TIMEOUT_MS - elapsed_ms);
}

// Expiration can be observed from the timer task or from a public accessor.
// Return the last reason through a local variable so the event write always
// happens after releasing the state mutex.
static bool factory_reset_expire_with_reason_locked(
    factory_reset_reason_t *reason_out
) {
    if (!factory_reset_pending_is_expired_locked()) {
        return false;
    }
    if (reason_out != NULL) {
        *reason_out = factory_reset_pending_reason;
    }
    factory_reset_expire_locked();
    return true;
}

#if FACTORY_RESET_HAS_CONFIG_STORE
static esp_err_t factory_reset_erase_config_store(void) {
    if (!config_store_is_ready()) {
        // The NVS partition erase below still removes the namespace's data.
        // Report the result only when config_store is present and usable.
        return ESP_OK;
    }

    const char *const keys[] = {
        FACTORY_RESET_CONFIG_KEY_WIFI_SSID,
        FACTORY_RESET_CONFIG_KEY_WIFI_PASSWORD,
        FACTORY_RESET_CONFIG_KEY_PROVISIONING_SALT,
        FACTORY_RESET_CONFIG_KEY_PROVISIONING_VERIFIER,
        FACTORY_RESET_CONFIG_KEY_PROVISIONING_POP,
        FACTORY_RESET_CONFIG_KEY_PLATFORM_URL,
        FACTORY_RESET_CONFIG_KEY_DEVICE_BOUND,
        FACTORY_RESET_CONFIG_KEY_DEVICE_REGISTERED,
        FACTORY_RESET_CONFIG_KEY_DEVICE_SESSION,
        FACTORY_RESET_CONFIG_KEY_DEVICE_REGISTRATION_TOKEN,
    };
    esp_err_t first_error = ESP_OK;
    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const esp_err_t result = config_store_erase_key(keys[index]);
        if (result != ESP_OK && first_error == ESP_OK) {
            first_error = result;
        }
    }
    return first_error;
}
#endif

static esp_err_t factory_reset_erase_device_storage(void) {
#if FACTORY_RESET_HAS_CONFIG_STORE
    const esp_err_t store_result = factory_reset_erase_config_store();
#else
    const esp_err_t store_result = ESP_OK;
#endif

    const esp_err_t erase_result = nvs_flash_erase_partition(
        CONFIG_FACTORY_RESET_NVS_PARTITION
    );
    if (erase_result != ESP_OK &&
        erase_result != ESP_ERR_NVS_NOT_INITIALIZED) {
        return store_result != ESP_OK ? store_result : erase_result;
    }

    // nvs_flash_erase_partition deinitializes the partition. Reinitialize the
    // same device-owned NVS label so later writes start from a clean store.
    esp_err_t init_result = strcmp(
        CONFIG_FACTORY_RESET_NVS_PARTITION,
        NVS_DEFAULT_PART_NAME
    ) == 0
        ? nvs_flash_init()
        : nvs_flash_init_partition(CONFIG_FACTORY_RESET_NVS_PARTITION);
    if (init_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        init_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        init_result = nvs_flash_erase_partition(
            CONFIG_FACTORY_RESET_NVS_PARTITION
        );
        if (init_result == ESP_OK) {
            init_result = strcmp(
                CONFIG_FACTORY_RESET_NVS_PARTITION,
                NVS_DEFAULT_PART_NAME
            ) == 0
                ? nvs_flash_init()
                : nvs_flash_init_partition(
                    CONFIG_FACTORY_RESET_NVS_PARTITION
                );
        }
    }
    if (init_result != ESP_OK) {
        return store_result != ESP_OK ? store_result : init_result;
    }
    // A successful partition erase removes every namespace, so a prior
    // individual key-erase error does not leave device data behind.
    return ESP_OK;
}

static factory_reset_error_t factory_reset_perform_locked(void) {
    if (!factory_reset_pending) {
        return FACTORY_RESET_ERR_NO_PENDING_RESET;
    }
    if (factory_reset_pending_is_expired_locked()) {
        factory_reset_expire_locked();
        return FACTORY_RESET_ERR_EXPIRED;
    }

    factory_reset_pending = false;
    const esp_err_t erase_result = factory_reset_erase_device_storage();
    if (erase_result != ESP_OK) {
        ESP_LOGE(TAG, "device configuration erase failed: %s",
                 esp_err_to_name(erase_result));
        factory_reset_last_result = FACTORY_RESET_ERR_STORAGE;
        return factory_reset_last_result;
    }

    factory_reset_completed_count++;
    factory_reset_last_result = FACTORY_RESET_OK;
    return factory_reset_last_result;
}

static void factory_reset_timeout_callback(void *argument) {
    (void)argument;
    if (factory_reset_mutex == NULL) {
        return;
    }
    bool expired = false;
    factory_reset_reason_t reason = FACTORY_RESET_REASON_GUARDIAN_REQUEST;
    if (xSemaphoreTake(factory_reset_mutex, portMAX_DELAY) == pdTRUE) {
        expired = factory_reset_expire_with_reason_locked(&reason);
        xSemaphoreGive(factory_reset_mutex);
    }
    if (expired) {
        factory_reset_report_event(
            "factory_reset_cancelled",
            factory_reset_reason_name(reason)
        );
    }
}

static bool factory_reset_acquire(void) {
    return factory_reset_ready && factory_reset_mutex != NULL &&
        xSemaphoreTake(factory_reset_mutex, portMAX_DELAY) == pdTRUE;
}

static void factory_reset_release(void) {
    xSemaphoreGive(factory_reset_mutex);
}

static esp_err_t factory_reset_init(void) {
    if (factory_reset_ready) {
        return ESP_OK;
    }

    if (factory_reset_mutex == NULL) {
        factory_reset_mutex = xSemaphoreCreateMutex();
        if (factory_reset_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    factory_reset_pending = false;
    factory_reset_pending_reason = FACTORY_RESET_REASON_GUARDIAN_REQUEST;
    factory_reset_requested_at_ms = 0;
    factory_reset_last_result = FACTORY_RESET_OK;
    factory_reset_completed_count = 0;

    const esp_timer_create_args_t timer_config = {
        .callback = factory_reset_timeout_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "factory_reset",
        .skip_unhandled_events = false,
    };
    const esp_err_t timer_result = esp_timer_create(
        &timer_config,
        &factory_reset_timer
    );
    if (timer_result != ESP_OK) {
        vSemaphoreDelete(factory_reset_mutex);
        factory_reset_mutex = NULL;
        return timer_result;
    }

    factory_reset_ready = true;
    ESP_LOGI(TAG, "guarded factory reset ready");
    return ESP_OK;
}

factory_reset_error_t factory_reset_request(factory_reset_reason_t reason) {
    if (!factory_reset_reason_is_valid(reason)) {
        return FACTORY_RESET_ERR_INVALID_REASON;
    }
    if (!factory_reset_acquire()) {
        return FACTORY_RESET_ERR_NOT_INITIALIZED;
    }

    factory_reset_pending = true;
    factory_reset_pending_reason = reason;
    factory_reset_requested_at_ms = factory_reset_now_ms();
    factory_reset_last_result = FACTORY_RESET_OK;
    // Re-requesting while armed replaces the reason and restarts the bounded
    // timeout instead of failing because the one-shot timer is still active.
    (void)esp_timer_stop(factory_reset_timer);
    const esp_err_t timer_result = esp_timer_start_once(
        factory_reset_timer,
        (uint64_t)FACTORY_RESET_TIMEOUT_MS * 1000ULL
    );
    if (timer_result != ESP_OK) {
        factory_reset_pending = false;
        factory_reset_last_result = FACTORY_RESET_ERR_STORAGE;
        factory_reset_release();
        factory_reset_report_event(
            "factory_reset_failed",
            "factory_reset_storage_error"
        );
        return FACTORY_RESET_ERR_STORAGE;
    }
    factory_reset_release();
    factory_reset_report_event(
        "factory_reset_requested",
        factory_reset_reason_name(reason)
    );
    return FACTORY_RESET_OK;
}

factory_reset_error_t factory_reset_confirm(void) {
    if (!factory_reset_acquire()) {
        return FACTORY_RESET_ERR_NOT_INITIALIZED;
    }

    if (!factory_reset_pending) {
        factory_reset_release();
        return FACTORY_RESET_ERR_NO_PENDING_RESET;
    }
    if (factory_reset_pending_is_expired_locked()) {
        const factory_reset_reason_t reason = factory_reset_pending_reason;
        factory_reset_expire_locked();
        factory_reset_release();
        factory_reset_report_event(
            "factory_reset_cancelled",
            factory_reset_reason_name(reason)
        );
        return FACTORY_RESET_ERR_EXPIRED;
    }

    (void)esp_timer_stop(factory_reset_timer);
    const factory_reset_reason_t reason = factory_reset_pending_reason;
    const factory_reset_error_t result = factory_reset_perform_locked();
    factory_reset_release();
    factory_reset_report_event(
        result == FACTORY_RESET_OK
            ? "factory_reset_completed"
            : "factory_reset_failed",
        result == FACTORY_RESET_OK
            ? factory_reset_reason_name(reason)
            : factory_reset_error_name(result)
    );
    return result;
}

factory_reset_error_t factory_reset_cancel(void) {
    if (!factory_reset_acquire()) {
        return FACTORY_RESET_ERR_NOT_INITIALIZED;
    }

    const factory_reset_error_t result =
        factory_reset_pending ? FACTORY_RESET_OK
                              : FACTORY_RESET_ERR_NO_PENDING_RESET;
    const factory_reset_reason_t reason = factory_reset_pending_reason;
    factory_reset_pending = false;
    factory_reset_last_result = result;
    (void)esp_timer_stop(factory_reset_timer);
    factory_reset_release();
    if (result == FACTORY_RESET_OK) {
        factory_reset_report_event(
            "factory_reset_cancelled",
            factory_reset_reason_name(reason)
        );
    }
    return result;
}

bool factory_reset_is_pending(void) {
    if (!factory_reset_acquire()) {
        return false;
    }
    bool expired = false;
    factory_reset_reason_t expired_reason =
        FACTORY_RESET_REASON_GUARDIAN_REQUEST;
    if (factory_reset_pending_is_expired_locked()) {
        expired = factory_reset_expire_with_reason_locked(&expired_reason);
    }
    const bool pending = factory_reset_pending;
    factory_reset_release();
    if (expired) {
        factory_reset_report_event(
            "factory_reset_cancelled",
            factory_reset_reason_name(expired_reason)
        );
    }
    return pending;
}

factory_reset_snapshot_t factory_reset_get_snapshot(void) {
    factory_reset_snapshot_t snapshot = {
        .is_pending = false,
        .request_reason = FACTORY_RESET_REASON_GUARDIAN_REQUEST,
        .requested_at_ms = 0,
        .remaining_ms = 0,
        .last_result = FACTORY_RESET_ERR_NOT_INITIALIZED,
        .completed_count = 0,
        .is_ready = false,
    };
    if (!factory_reset_acquire()) {
        return snapshot;
    }
    bool expired = false;
    factory_reset_reason_t expired_reason =
        FACTORY_RESET_REASON_GUARDIAN_REQUEST;
    if (factory_reset_pending_is_expired_locked()) {
        expired = factory_reset_expire_with_reason_locked(&expired_reason);
    }
    snapshot.is_pending = factory_reset_pending;
    snapshot.request_reason = factory_reset_pending_reason;
    snapshot.requested_at_ms = factory_reset_requested_at_ms;
    snapshot.remaining_ms = factory_reset_remaining_ms_locked();
    snapshot.last_result = factory_reset_last_result;
    snapshot.completed_count = factory_reset_completed_count;
    snapshot.is_ready = factory_reset_ready;
    factory_reset_release();
    if (expired) {
        factory_reset_report_event(
            "factory_reset_cancelled",
            factory_reset_reason_name(expired_reason)
        );
    }
    return snapshot;
}

const char *factory_reset_reason_name(factory_reset_reason_t reason) {
    switch (reason) {
        case FACTORY_RESET_REASON_GUARDIAN_REQUEST:
            return "guardian_request";
        case FACTORY_RESET_REASON_BUTTON_GESTURE:
            return "button_gesture";
        case FACTORY_RESET_REASON_PROVISIONING_RESET:
            return "provisioning_reset";
        case FACTORY_RESET_REASON_INTERNAL_RECOVERY:
            return "internal_recovery";
        default:
            return "unknown";
    }
}

const char *factory_reset_error_name(factory_reset_error_t error) {
    switch (error) {
        case FACTORY_RESET_OK:
            return "ok";
        case FACTORY_RESET_ERR_NOT_INITIALIZED:
            return "factory_reset_not_initialized";
        case FACTORY_RESET_ERR_INVALID_REASON:
            return "factory_reset_invalid_reason";
        case FACTORY_RESET_ERR_NO_PENDING_RESET:
            return "factory_reset_no_pending_request";
        case FACTORY_RESET_ERR_EXPIRED:
            return "factory_reset_expired";
        case FACTORY_RESET_ERR_STORAGE:
        default:
            return "factory_reset_storage_error";
    }
}

static void factory_reset_shutdown(void) {
    if (!factory_reset_ready && factory_reset_mutex == NULL &&
        factory_reset_timer == NULL) {
        return;
    }

    factory_reset_ready = false;
    if (factory_reset_mutex != NULL &&
        xSemaphoreTake(factory_reset_mutex, portMAX_DELAY) == pdTRUE) {
        factory_reset_pending = false;
        xSemaphoreGive(factory_reset_mutex);
    }

    if (factory_reset_timer != NULL) {
        // Disarm and wait for an in-flight callback before deleting the
        // mutex that callback uses.
        (void)esp_timer_stop_blocking(factory_reset_timer, portMAX_DELAY);
        (void)esp_timer_delete(factory_reset_timer);
        factory_reset_timer = NULL;
    }

    if (factory_reset_mutex != NULL) {
        vSemaphoreDelete(factory_reset_mutex);
        factory_reset_mutex = NULL;
    }
    factory_reset_pending = false;
    factory_reset_completed_count = 0;
    factory_reset_last_result = FACTORY_RESET_OK;
}

const module_descriptor_t *factory_reset_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "factory_reset",
        .version = "1.0.0",
        .initialize = factory_reset_init,
        .shutdown = factory_reset_shutdown,
    };
    return &descriptor;
}
