#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_SNTP_OPMODE_POLL = 0,
    ESP_SNTP_OPMODE_LISTENONLY,
} esp_sntp_operatingmode_t;

typedef void (*esp_sntp_time_sync_notification_cb_t)(struct timeval *tv);

typedef enum {
    SNTP_SYNC_STATUS_RESET = 0,
    SNTP_SYNC_STATUS_COMPLETED,
    SNTP_SYNC_STATUS_IN_PROGRESS,
} sntp_sync_status_t;

void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t operating_mode);
void esp_sntp_setservername(uint8_t index, const char *server);
void esp_sntp_set_time_sync_notification_cb(
    esp_sntp_time_sync_notification_cb_t callback
);
void esp_sntp_set_sync_status(sntp_sync_status_t status);
sntp_sync_status_t esp_sntp_get_sync_status(void);
void esp_sntp_init(void);
void esp_sntp_stop(void);
void esp_sntp_restart(void);

#ifdef __cplusplus
}
#endif
