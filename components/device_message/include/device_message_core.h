#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_MESSAGE_TITLE_MAX_RUNES 120
#define DEVICE_MESSAGE_BODY_MAX_RUNES 2000
#define DEVICE_MESSAGE_MIN_DURATION_SECONDS 1
#define DEVICE_MESSAGE_MAX_DURATION_SECONDS 300
#define DEVICE_MESSAGE_DEFAULT_DURATION_SECONDS 30

/** @brief Pure field validation shared by runtime and host tests. */
bool device_message_title_is_valid(const char *title);
bool device_message_body_is_valid(const char *body);
bool device_message_severity_is_valid(const char *severity);

/** @brief Clamp a requested duration into the supported bounds. */
uint32_t device_message_clamp_duration(uint32_t requested_seconds);

/** @brief Count UTF-8 runes in a NUL-terminated string. */
size_t device_message_utf8_runes(const char *value);

#ifdef __cplusplus
}
#endif
