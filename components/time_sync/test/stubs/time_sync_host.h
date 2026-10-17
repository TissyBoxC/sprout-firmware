#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_sntp.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void time_sync_host_reset(void);
void time_sync_host_reset_module(void);
void time_sync_host_set_store_ready(bool ready);
void time_sync_host_set_string(const char *key, const char *value);
void time_sync_host_set_set_result(const char *key, esp_err_t result);
void time_sync_host_set_clock_result(int result);
void time_sync_host_set_network_state(network_manager_state_t state);
void time_sync_host_set_callback_result(esp_err_t result);
esp_err_t time_sync_host_fire_network_state(network_manager_state_t state);
void time_sync_host_fire_sntp(int64_t epoch_seconds);
int64_t time_sync_host_clock_epoch(void);
int time_sync_host_clock_calls(void);
int time_sync_host_sntp_init_calls(void);
int time_sync_host_sntp_stop_calls(void);
int time_sync_host_sntp_restart_calls(void);
const char *time_sync_host_sntp_server(void);
esp_sntp_operatingmode_t time_sync_host_sntp_mode(void);
sntp_sync_status_t time_sync_host_sntp_status(void);
const char *time_sync_host_string(const char *key);
int time_sync_host_set_string_calls(const char *key);

#ifdef __cplusplus
}
#endif
