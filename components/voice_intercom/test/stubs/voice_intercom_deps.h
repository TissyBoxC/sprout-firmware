#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

/**
 * @brief Reset every dependency double and the module state between cases.
 *
 * The module has no host-only entry point, so a reset reinitializes it after
 * the doubles are restored to their defaults.
 */
void voice_intercom_host_reset(void);

void voice_intercom_host_set_network_connected(bool is_connected);
void voice_intercom_host_set_broker_url(const char *broker_url);
void voice_intercom_host_set_identity_available(bool is_available);
void voice_intercom_host_set_session_ready(bool is_ready);
void voice_intercom_host_set_session_start_result(int result);

/** @brief Simulate the realtime session holding the capture lease. */
void voice_intercom_host_set_capture_enabled(bool is_enabled);

/** @brief Drop the simulated capture lease without a session record. */
void voice_intercom_host_set_capture_drops_after_start(void);

/** @brief Force the next `voice_session_start_session` to report success
 *  while the capture lease is not actually held. */
void voice_intercom_host_set_capture_missing(void);

void voice_intercom_host_deliver_mqtt_data(const char *payload, int length);

int voice_intercom_host_session_start_calls(void);
int voice_intercom_host_session_end_calls(void);
int voice_intercom_host_session_shutdown_calls(void);
int voice_intercom_host_capture_stop_calls(void);
int voice_intercom_host_capture_lease_release_calls(void);
int voice_intercom_host_output_flush_calls(void);
int voice_intercom_host_prompt_tone_calls(void);
int voice_intercom_host_display_calls(void);
int voice_intercom_host_mqtt_init_calls(void);
int voice_intercom_host_mqtt_stop_calls(void);
int voice_intercom_host_mqtt_destroy_calls(void);
int voice_intercom_host_mqtt_subscribe_calls(void);
int voice_intercom_host_mqtt_publish_calls(void);

esp_mqtt_client_handle_t voice_intercom_host_mqtt_client(void);
const char *voice_intercom_host_mqtt_published_topic(void);
const char *voice_intercom_host_mqtt_published_payload(void);
const char *voice_intercom_host_display_body(void);
int voice_intercom_host_led_state(void);

#ifdef __cplusplus
}
#endif
