#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "bluetooth_audio.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reset every stub-backed dependency and the host request log. */
void bluetooth_audio_sync_host_reset(void);
void bluetooth_audio_sync_host_reset_audio_state(void);

void bluetooth_audio_sync_host_set_network_state(
    network_manager_state_t state
);
void bluetooth_audio_sync_host_set_time_synchronized(bool synchronized);
void bluetooth_audio_sync_host_set_registered(bool registered);
void bluetooth_audio_sync_host_set_session_token(const char *token);
void bluetooth_audio_sync_host_set_device_id(const char *device_id);
void bluetooth_audio_sync_host_set_base_url(const char *base_url);
void bluetooth_audio_sync_host_set_copy_session_result(esp_err_t result);
void bluetooth_audio_sync_host_set_identity_result(esp_err_t result);
void bluetooth_audio_sync_host_set_base_url_result(esp_err_t result);
void bluetooth_audio_sync_host_set_transport_security_result(esp_err_t result);

/**
 * Queue the status/body pairs returned by successive HTTP requests.
 *
 * A sync cycle consumes the policy response first, then the best-effort status
 * report response.
 */
void bluetooth_audio_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void bluetooth_audio_sync_host_set_perform_result(esp_err_t result);
void bluetooth_audio_sync_host_set_perform_result_at(
    size_t request_index,
    esp_err_t result
);
void bluetooth_audio_sync_host_set_client_init_fails(bool fails);
void bluetooth_audio_sync_host_set_task_create_fails(bool fails);

/**
 * Stop the worker after a fixed number of vTaskDelay calls.
 *
 * The worker loop is infinite, so a host test has to bound it. Passing 0 stops
 * it after the first delay; passing -1 disables the automatic stop.
 */
void bluetooth_audio_sync_host_set_stop_after_delays(int delay_count);

int bluetooth_audio_sync_host_clear_session_calls(void);
int bluetooth_audio_sync_host_transport_security_calls(void);
int bluetooth_audio_sync_host_task_create_calls(void);
int bluetooth_audio_sync_host_task_delete_calls(void);
int bluetooth_audio_sync_host_delay_calls(void);
bool bluetooth_audio_sync_host_task_pending(void);

size_t bluetooth_audio_sync_host_request_count(void);
const char *bluetooth_audio_sync_host_request_url(size_t index);
const char *bluetooth_audio_sync_host_request_body(size_t index);
const char *bluetooth_audio_sync_host_request_authorization(size_t index);
esp_http_client_method_t bluetooth_audio_sync_host_request_method(
    size_t index
);

/** Run the worker task captured by the last xTaskCreate call, once. */
void bluetooth_audio_sync_host_run_pending_task(void);

/** Production hooks compiled into bluetooth_audio_sync.cpp for the host test. */
void bluetooth_audio_sync_host_reset_module(void);
void bluetooth_audio_sync_host_set_stop(bool stop);
bool bluetooth_audio_sync_host_is_ready(void);

#ifdef __cplusplus
}
#endif
