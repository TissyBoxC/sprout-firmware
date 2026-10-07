#include "conversation_context.h"

#include <string.h>

#include "conversation_context_ring.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    bool is_initialized;
    conversation_context_ring_t ring;
    SemaphoreHandle_t mutex;
} conversation_context_state_t;

static conversation_context_state_t conversation_context_state;

static uint64_t conversation_context_now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static bool conversation_context_lock(void) {
    return conversation_context_state.mutex != NULL &&
           xSemaphoreTake(
               conversation_context_state.mutex,
               portMAX_DELAY
           ) == pdTRUE;
}

static void conversation_context_unlock(void) {
    xSemaphoreGive(conversation_context_state.mutex);
}

esp_err_t conversation_context_init(void) {
    if (conversation_context_state.mutex == NULL) {
        conversation_context_state.mutex = xSemaphoreCreateMutex();
        if (conversation_context_state.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!conversation_context_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!conversation_context_state.is_initialized) {
        conversation_context_ring_reset(&conversation_context_state.ring);
        conversation_context_state.is_initialized = true;
    }
    conversation_context_unlock();
    return ESP_OK;
}

bool conversation_context_is_ready(void) {
    return conversation_context_state.is_initialized;
}

esp_err_t conversation_context_append(
    const char *role,
    const char *summary
) {
    if (summary == NULL || summary[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!conversation_context_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!conversation_context_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool appended = conversation_context_ring_append(
        &conversation_context_state.ring,
        role,
        summary,
        conversation_context_now_ms()
    );
    conversation_context_unlock();
    return appended ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t conversation_context_get_recent(
    conversation_context_turn_t *turns_out,
    size_t max_turns,
    size_t *turn_count_out
) {
    if (turns_out == NULL || turn_count_out == NULL || max_turns == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!conversation_context_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!conversation_context_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    *turn_count_out = conversation_context_ring_recent(
        &conversation_context_state.ring,
        turns_out,
        max_turns
    );
    conversation_context_unlock();
    return ESP_OK;
}

esp_err_t conversation_context_clear(void) {
    if (!conversation_context_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!conversation_context_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    conversation_context_ring_reset(&conversation_context_state.ring);
    conversation_context_unlock();
    return ESP_OK;
}

esp_err_t conversation_context_get_snapshot(
    conversation_context_snapshot_t *snapshot_out
) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!conversation_context_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!conversation_context_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    snapshot_out->is_initialized = conversation_context_state.is_initialized;
    snapshot_out->turn_count =
        (uint32_t)conversation_context_state.ring.count;
    snapshot_out->total_appended =
        conversation_context_state.ring.total_appended;
    snapshot_out->evicted = conversation_context_state.ring.evicted;
    snapshot_out->next_sequence =
        conversation_context_state.ring.next_sequence;
    conversation_context_unlock();
    return ESP_OK;
}

void conversation_context_shutdown(void) {
    if (conversation_context_state.mutex == NULL) {
        return;
    }
    if (!conversation_context_lock()) {
        return;
    }
    conversation_context_ring_reset(&conversation_context_state.ring);
    conversation_context_state.is_initialized = false;
    conversation_context_unlock();
}

const module_descriptor_t *conversation_context_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "conversation_context",
        .version = "1.0.0",
        .initialize = conversation_context_init,
        .shutdown = conversation_context_shutdown,
    };
    return &descriptor;
}
