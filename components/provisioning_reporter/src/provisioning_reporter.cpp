#include "provisioning_reporter.h"
#include "provisioning_reporter_state.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "version_info.h"

#define PROVISIONING_REPORTER_NVS_KEY "prov_state"
#define PROVISIONING_REPORTER_MAXIMUM_DURATION_MS \
    (24u * 60u * 60u * 1000u)

static_assert(
    sizeof(PROVISIONING_REPORTER_NVS_KEY) <= NVS_KEY_NAME_MAX_SIZE,
    "provisioning state key must fit the ESP-IDF NVS key limit"
);
static_assert(
    sizeof(provisioning_reporter_state_t) <=
        PROVISIONING_REPORTER_MAX_STATE_SIZE,
    "provisioning state must remain a bounded NVS blob"
);

static bool provisioning_reporter_ready;
static SemaphoreHandle_t provisioning_reporter_mutex;
static provisioning_reporter_state_t provisioning_reporter_state;

static bool provisioning_reporter_lock(void) {
    return provisioning_reporter_mutex != NULL &&
        xSemaphoreTake(provisioning_reporter_mutex, portMAX_DELAY) == pdTRUE;
}

static void provisioning_reporter_unlock(void) {
    if (provisioning_reporter_mutex != NULL) {
        (void)xSemaphoreGive(provisioning_reporter_mutex);
    }
}

static void provisioning_reporter_copy_text(
    char *output,
    size_t output_size,
    const char *input
) {
    if (output == NULL || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == NULL) {
        return;
    }

    size_t output_index = 0;
    while (input[output_index] != '\0' &&
           output_index + 1 < output_size) {
        const unsigned char value = (unsigned char)input[output_index];
        output[output_index] =
            value >= 0x20 && value <= 0x7e ? (char)value : '_';
        ++output_index;
    }
    output[output_index] = '\0';
}

static void provisioning_reporter_clear_state_locked(void) {
    memset(
        &provisioning_reporter_state,
        0,
        sizeof(provisioning_reporter_state)
    );
    provisioning_reporter_state.magic = PROVISIONING_REPORTER_STATE_MAGIC;
    provisioning_reporter_state.version =
        PROVISIONING_REPORTER_STATE_VERSION;
    provisioning_reporter_state.next_sequence = 1;
}

static esp_err_t provisioning_reporter_save_state_locked(void) {
    return config_store_set_blob(
        PROVISIONING_REPORTER_NVS_KEY,
        &provisioning_reporter_state,
        sizeof(provisioning_reporter_state)
    );
}

static esp_err_t provisioning_reporter_load_state_locked(void) {
    uint8_t raw_state[sizeof(provisioning_reporter_state)] = {};
    size_t state_size = sizeof(raw_state);
    const esp_err_t result = config_store_get_blob(
        PROVISIONING_REPORTER_NVS_KEY,
        raw_state,
        &state_size
    );
    if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        return result;
    }
    if (result != ESP_OK || state_size != sizeof(provisioning_reporter_state)) {
        provisioning_reporter_clear_state_locked();
        return result != ESP_OK ? result : ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        &provisioning_reporter_state,
        raw_state,
        sizeof(provisioning_reporter_state)
    );
    if (!provisioning_reporter_state_is_valid(
            &provisioning_reporter_state
        )) {
        provisioning_reporter_clear_state_locked();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static uint32_t provisioning_reporter_next_sequence_locked(void) {
    uint32_t sequence = provisioning_reporter_state.next_sequence;
    if (sequence == 0 || sequence == UINT32_MAX) {
        sequence = 1;
    }
    provisioning_reporter_state.next_sequence = sequence + 1;
    return sequence;
}

static void provisioning_reporter_append_event_locked(
    const provisioning_reporter_event_t *event
) {
    if (provisioning_reporter_state.event_count ==
        PROVISIONING_REPORTER_EVENT_CAPACITY) {
        memmove(
            &provisioning_reporter_state.events[0],
            &provisioning_reporter_state.events[1],
            sizeof(provisioning_reporter_state.events[0]) *
                (PROVISIONING_REPORTER_EVENT_CAPACITY - 1)
        );
        --provisioning_reporter_state.event_count;
        ++provisioning_reporter_state.dropped;
    }
    provisioning_reporter_state.events
        [provisioning_reporter_state.event_count++] = *event;
}

static void provisioning_reporter_update_status_locked(
    provisioning_event_type_t type,
    const char *detail_code,
    int64_t now_epoch
) {
    provisioning_reporter_copy_text(
        provisioning_reporter_state.last_detail_code,
        sizeof(provisioning_reporter_state.last_detail_code),
        detail_code
    );
    if (type == PROVISIONING_EVENT_WIFI_CONFIGURED) {
        provisioning_reporter_state.wifi_configured = 1;
        provisioning_reporter_state.last_provisioned_epoch = now_epoch;
    } else if (type == PROVISIONING_EVENT_BINDING_COMPLETED ||
               type == PROVISIONING_EVENT_BINDING_CONFIRMED) {
        provisioning_reporter_state.last_provisioned_epoch = now_epoch;
    }
}

esp_err_t provisioning_reporter_init(void) {
    if (provisioning_reporter_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    provisioning_reporter_mutex = xSemaphoreCreateMutex();
    if (provisioning_reporter_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!provisioning_reporter_lock()) {
        vSemaphoreDelete(provisioning_reporter_mutex);
        provisioning_reporter_mutex = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = provisioning_reporter_load_state_locked();
    if (result == CONFIG_STORE_ERR_NOT_FOUND ||
        result == ESP_ERR_INVALID_STATE ||
        result == ESP_ERR_INVALID_SIZE) {
        provisioning_reporter_clear_state_locked();
        result = provisioning_reporter_save_state_locked();
    }
    provisioning_reporter_unlock();

    if (result != ESP_OK) {
        vSemaphoreDelete(provisioning_reporter_mutex);
        provisioning_reporter_mutex = NULL;
        memset(
            &provisioning_reporter_state,
            0,
            sizeof(provisioning_reporter_state)
        );
        return result;
    }
    provisioning_reporter_ready = true;
    return ESP_OK;
}

void provisioning_reporter_record(
    provisioning_event_type_t type,
    const char *detail_code,
    uint32_t duration_ms
) {
    if (!provisioning_reporter_ready ||
        !provisioning_reporter_event_type_is_valid(type) ||
        !provisioning_reporter_detail_code_is_valid(detail_code) ||
        duration_ms > PROVISIONING_REPORTER_MAXIMUM_DURATION_MS) {
        return;
    }
    if (!provisioning_reporter_lock()) {
        return;
    }

    const provisioning_reporter_state_t previous_state =
        provisioning_reporter_state;
    provisioning_reporter_event_t event = {};
    event.sequence = provisioning_reporter_next_sequence_locked();
    event.duration_ms = duration_ms;
    snprintf(
        event.event_id,
        sizeof(event.event_id),
        "provisioning_%08lx",
        (unsigned long)event.sequence
    );
    provisioning_reporter_copy_text(
        event.event_type,
        sizeof(event.event_type),
        provisioning_reporter_event_type_name(type)
    );
    provisioning_reporter_copy_text(
        event.detail_code,
        sizeof(event.detail_code),
        detail_code
    );
    const version_info_t version = version_info_get();
    provisioning_reporter_copy_text(
        event.firmware_version,
        sizeof(event.firmware_version),
        version.firmware_version
    );
    provisioning_reporter_update_status_locked(
        type,
        event.detail_code,
        time(NULL)
    );
    provisioning_reporter_append_event_locked(&event);

    if (!provisioning_reporter_state_is_valid(
            &provisioning_reporter_state
        )) {
        provisioning_reporter_state = previous_state;
        provisioning_reporter_unlock();
        return;
    }
    if (provisioning_reporter_save_state_locked() != ESP_OK) {
        provisioning_reporter_state = previous_state;
    }
    provisioning_reporter_unlock();
}

esp_err_t provisioning_reporter_get_snapshot(
    provisioning_reporter_snapshot_t *snapshot
) {
    if (!provisioning_reporter_ready || snapshot == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!provisioning_reporter_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->event_count = provisioning_reporter_state.event_count;
    snapshot->dropped = provisioning_reporter_state.dropped;
    for (size_t index = 0;
         index < provisioning_reporter_state.event_count;
         ++index) {
        snapshot->events[index] = provisioning_reporter_state.events[index];
    }
    if (snapshot->event_count > 0) {
        snapshot->newest_sequence =
            snapshot->events[snapshot->event_count - 1].sequence;
    }
    provisioning_reporter_unlock();
    return ESP_OK;
}

esp_err_t provisioning_reporter_acknowledge(uint32_t through_sequence) {
    if (!provisioning_reporter_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (through_sequence == 0) {
        return ESP_OK;
    }
    if (!provisioning_reporter_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    const provisioning_reporter_state_t previous_state =
        provisioning_reporter_state;
    (void)provisioning_reporter_acknowledge_state(
        &provisioning_reporter_state,
        through_sequence
    );
    const esp_err_t result = provisioning_reporter_save_state_locked();
    if (result != ESP_OK) {
        provisioning_reporter_state = previous_state;
    }
    provisioning_reporter_unlock();
    return result;
}

esp_err_t provisioning_reporter_get_status(
    provisioning_reporter_status_t *status
) {
    if (!provisioning_reporter_ready || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!provisioning_reporter_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(status, 0, sizeof(*status));
    status->wifi_configured =
        provisioning_reporter_state.wifi_configured != 0;
    status->last_provisioned_epoch =
        provisioning_reporter_state.last_provisioned_epoch;
    provisioning_reporter_copy_text(
        status->last_detail_code,
        sizeof(status->last_detail_code),
        provisioning_reporter_state.last_detail_code
    );
    provisioning_reporter_unlock();
    return ESP_OK;
}

void provisioning_reporter_shutdown(void) {
    if (provisioning_reporter_mutex != NULL) {
        vSemaphoreDelete(provisioning_reporter_mutex);
        provisioning_reporter_mutex = NULL;
    }
    provisioning_reporter_ready = false;
}

const module_descriptor_t *provisioning_reporter_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "provisioning_reporter",
        .version = "1.0.0",
        .initialize = provisioning_reporter_init,
        .shutdown = provisioning_reporter_shutdown,
    };
    return &descriptor;
}
