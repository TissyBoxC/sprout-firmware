#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_EVENT_SEQUENCE_OK = 0,
    OTA_EVENT_SEQUENCE_INVALID_ARGUMENT,
    OTA_EVENT_SEQUENCE_NOT_FIRST,
    OTA_EVENT_SEQUENCE_INVALID_TRANSITION,
} ota_event_sequence_error_t;

/** @brief Append one platform OTA event type to a bounded sequence. */
ota_event_sequence_error_t ota_event_sequence_append(
    const char **events,
    size_t event_count,
    size_t capacity,
    const char *event_type
);

/** @brief Validate the canonical success sequence. */
bool ota_event_sequence_is_success(
    const char *const *events,
    size_t event_count
);

/** @brief Validate that failure and rollback sequences remain terminal. */
bool ota_event_sequence_is_terminal(
    const char *const *events,
    size_t event_count
);

#ifdef __cplusplus
}
#endif
