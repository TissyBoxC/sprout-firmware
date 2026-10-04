#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#if CONFIG_FEATURE_ERROR_CODE
#include "error_code.h"
#endif
#if CONFIG_FEATURE_CONFIG_STORE
#include "config_store.h"
#endif
#if CONFIG_FEATURE_CLOUD_AUTH
#include "cloud_auth.h"
#endif
#if CONFIG_FEATURE_AUDIO_CODEC
#include "audio_codec.h"
#endif
#if CONFIG_FEATURE_AUDIO_PIPELINE
#include "audio_pipeline.h"
#endif
#if CONFIG_FEATURE_AUDIO_INPUT
#include "audio_input.h"
#endif
#if CONFIG_FEATURE_VOICE_WAKE
#include "voice_wake.h"
#endif
#if CONFIG_FEATURE_WAKE_FEEDBACK
#include "wake_feedback.h"
#endif
#if CONFIG_FEATURE_BUTTON_INPUT
#include "button_input.h"
#endif
#if CONFIG_FEATURE_LED_INDICATOR
#include "led_indicator.h"
#endif
#if CONFIG_FEATURE_FACTORY_RESET
#include "factory_reset.h"
#endif
#if CONFIG_FEATURE_VOLUME_CONTROL
#include "volume_control.h"
#endif
#if CONFIG_FEATURE_AUDIO_OUTPUT
#include "audio_output.h"
#endif
#if CONFIG_FEATURE_PLAYBACK_QUEUE
#include "playback_queue.h"
#endif
#if CONFIG_FEATURE_PROMPT_TONE
#include "prompt_tone.h"
#endif
#if CONFIG_FEATURE_DEVICE_BINDING_CLIENT
#include "device_binding_client.h"
#endif
#if CONFIG_FEATURE_DEVICE_PROVISIONING
#include "device_provisioning.h"
#endif
#if CONFIG_FEATURE_DEVICE_IDENTITY
#include "device_identity.h"
#endif
#if CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER
#include "device_runtime_reporter.h"
#endif
#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER
#include "diagnostic_reporter.h"
#endif
#if CONFIG_FEATURE_DEVICE_CAPABILITIES
#include "device_capabilities.h"
#endif
#if CONFIG_FEATURE_ERROR_RECOVERY
#include "error_recovery.h"
#endif
#if CONFIG_FEATURE_MODULE_REGISTRY
#include "module_registry.h"
#endif
#if CONFIG_FEATURE_NETWORK_MANAGER
#include "network_manager.h"
#endif
#if CONFIG_FEATURE_NETWORK_QUALITY
#include "network_quality.h"
#endif
#if CONFIG_FEATURE_OFFLINE_FALLBACK
#include "offline_fallback.h"
#endif
#if CONFIG_FEATURE_PARENT_POLICY
#include "parent_policy.h"
#endif
#if CONFIG_FEATURE_PROVISIONING_PAYLOAD
#include "provisioning_payload.h"
#endif
#if CONFIG_FEATURE_SYSTEM_CORE
#include "system_core.h"
#endif
#if CONFIG_FEATURE_TIME_SYNC
#include "time_sync.h"
#endif
#if CONFIG_FEATURE_UI_TEXT
#include "ui_text.h"
#endif
#if CONFIG_FEATURE_VERSION_INFO
#include "version_info.h"
#endif

static const char *const TAG = "sprout_main";

#if CONFIG_FEATURE_FACTORY_RESET
// A cached sdkconfig from an older firmware revision may predate this Kconfig
// symbol. Fall back to the documented default so an incremental build still
// matches the erase-safety contract instead of failing to compile.
#ifndef CONFIG_FACTORY_RESET_LOCAL_CONFIRM_WINDOW_SECONDS
#define CONFIG_FACTORY_RESET_LOCAL_CONFIRM_WINDOW_SECONDS 5
#endif
#define FACTORY_RESET_LOCAL_CONFIRM_WINDOW_MS \
    (CONFIG_FACTORY_RESET_LOCAL_CONFIRM_WINDOW_SECONDS * 1000)

static bool factory_reset_local_confirm_armed;
static esp_timer_handle_t factory_reset_local_confirm_timer;

static void factory_reset_local_finish_cancelled(void) {
    factory_reset_local_confirm_armed = false;
#if CONFIG_FEATURE_LED_INDICATOR
    (void)led_indicator_set_state(LED_INDICATOR_STATE_IDLE);
#endif
#if CONFIG_FEATURE_PROMPT_TONE
    (void)prompt_tone_play(PROMPT_TONE_FACTORY_RESET_CANCELLED);
#endif
}

static void factory_reset_local_confirm_timeout(void *argument) {
    (void)argument;
    if (!factory_reset_local_confirm_armed) {
        return;
    }
    // factory_reset_cancel records the cancellation event itself; emitting it
    // here as well would create two audit records for one transition.
    (void)factory_reset_cancel();
    factory_reset_local_finish_cancelled();
}

static void factory_reset_local_timeout_start(void) {
    if (factory_reset_local_confirm_timer == NULL) {
        const esp_timer_create_args_t timer_config = {
            .callback = factory_reset_local_confirm_timeout,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "factory_reset_local",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(
                &timer_config,
                &factory_reset_local_confirm_timer
            ) != ESP_OK) {
            factory_reset_local_confirm_timer = NULL;
            return;
        }
    }
    (void)esp_timer_stop(factory_reset_local_confirm_timer);
    (void)esp_timer_start_once(
        factory_reset_local_confirm_timer,
        (uint64_t)FACTORY_RESET_LOCAL_CONFIRM_WINDOW_MS * 1000ULL
    );
}

static void factory_reset_local_timeout_stop(void) {
    if (factory_reset_local_confirm_timer != NULL) {
        (void)esp_timer_stop(factory_reset_local_confirm_timer);
    }
}

static void factory_reset_local_report_cancelled(void) {
    factory_reset_local_timeout_stop();
    factory_reset_local_finish_cancelled();
}

static void factory_reset_local_arm(void) {
    factory_reset_local_confirm_armed = true;
    factory_reset_local_timeout_start();
#if CONFIG_FEATURE_LED_INDICATOR
    (void)led_indicator_set_state(LED_INDICATOR_STATE_FACTORY_RESET);
#endif
#if CONFIG_FEATURE_PROMPT_TONE
    (void)prompt_tone_play(PROMPT_TONE_FACTORY_RESET_ARMED);
#endif
}

static void factory_reset_local_confirm(void) {
    if (!factory_reset_local_confirm_armed) {
        return;
    }
    factory_reset_local_confirm_armed = false;
    factory_reset_local_timeout_stop();
    const factory_reset_error_t result = factory_reset_confirm();
#if CONFIG_FEATURE_LED_INDICATOR
    (void)led_indicator_set_state(
        result == FACTORY_RESET_OK
            ? LED_INDICATOR_STATE_FACTORY_RESET
            : LED_INDICATOR_STATE_ERROR
    );
#endif
#if CONFIG_FEATURE_PROMPT_TONE
    (void)prompt_tone_play(
        result == FACTORY_RESET_OK
            ? PROMPT_TONE_FACTORY_RESET_COMPLETED
            : PROMPT_TONE_FACTORY_RESET_CANCELLED
    );
#endif
    if (result == FACTORY_RESET_OK) {
        esp_restart();
    }
}
#endif

#if CONFIG_FEATURE_BUTTON_INPUT && CONFIG_FEATURE_MODULE_REGISTRY
// The button task owns one handler for the whole application. Keeping the
// mapping here avoids coupling gesture policy to the input driver.
static void handle_button_gesture(const button_input_event_t *event) {
    if (event == NULL) {
        return;
    }
#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER
    (void)diagnostic_reporter_record_interaction(
        "button_gesture",
        button_input_gesture_name(event->gesture),
        event->held_ms
    );
#endif
#if CONFIG_FEATURE_FACTORY_RESET
    if (event->gesture == BUTTON_INPUT_GESTURE_VERY_LONG_PRESS) {
        // A very-long press only arms the guarded flow. The physical confirm
        // step is a short press of the other button within the local window.
        if (event->button_index == 0) {
            const factory_reset_error_t result =
                factory_reset_request(
                    FACTORY_RESET_REASON_BUTTON_GESTURE
                );
            if (result == FACTORY_RESET_OK) {
                factory_reset_local_arm();
            }
        }
    } else if (factory_reset_local_confirm_armed) {
        if (event->gesture == BUTTON_INPUT_GESTURE_SHORT_PRESS &&
            event->button_index == 1) {
            factory_reset_local_confirm();
        } else if (event->gesture == BUTTON_INPUT_GESTURE_SHORT_PRESS &&
                   event->button_index == 0) {
            (void)factory_reset_cancel();
            factory_reset_local_report_cancelled();
        }
    }
#endif
}
#endif

#if CONFIG_FEATURE_MODULE_REGISTRY
static void register_modules(void) {
#if CONFIG_FEATURE_SYSTEM_CORE
    ESP_ERROR_CHECK(module_registry_add(system_core_module_descriptor()));
#endif
#if CONFIG_FEATURE_VERSION_INFO
    ESP_ERROR_CHECK(module_registry_add(version_info_module_descriptor()));
#endif
#if CONFIG_FEATURE_ERROR_CODE
    ESP_ERROR_CHECK(module_registry_add(error_code_module_descriptor()));
#endif
#if CONFIG_FEATURE_DEVICE_IDENTITY
    ESP_ERROR_CHECK(module_registry_add(device_identity_module_descriptor()));
#endif
#if CONFIG_FEATURE_CONFIG_STORE
    ESP_ERROR_CHECK(module_registry_add(config_store_module_descriptor()));
#endif
#if CONFIG_FEATURE_NETWORK_MANAGER
    ESP_ERROR_CHECK(module_registry_add(network_manager_module_descriptor()));
#endif
#if CONFIG_FEATURE_TIME_SYNC
    ESP_ERROR_CHECK(module_registry_add(time_sync_module_descriptor()));
#endif
#if CONFIG_FEATURE_NETWORK_QUALITY
    ESP_ERROR_CHECK(module_registry_add(network_quality_module_descriptor()));
#endif
#if CONFIG_FEATURE_OFFLINE_FALLBACK
    ESP_ERROR_CHECK(module_registry_add(offline_fallback_module_descriptor()));
#endif
#if CONFIG_FEATURE_DEVICE_BINDING_CLIENT
    ESP_ERROR_CHECK(module_registry_add(device_binding_client_module_descriptor()));
#endif
#if CONFIG_FEATURE_DEVICE_PROVISIONING
    ESP_ERROR_CHECK(module_registry_add(device_provisioning_module_descriptor()));
#endif
#if CONFIG_FEATURE_CLOUD_AUTH
    ESP_ERROR_CHECK(module_registry_add(cloud_auth_module_descriptor()));
#endif
#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER
    ESP_ERROR_CHECK(module_registry_add(diagnostic_reporter_module_descriptor()));
#endif
#if CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER
    ESP_ERROR_CHECK(module_registry_add(device_runtime_reporter_module_descriptor()));
#endif
#if CONFIG_FEATURE_PROVISIONING_PAYLOAD
    ESP_ERROR_CHECK(module_registry_add(provisioning_payload_module_descriptor()));
#endif
#if CONFIG_FEATURE_DEVICE_CAPABILITIES
    ESP_ERROR_CHECK(module_registry_add(device_capabilities_module_descriptor()));
#endif
#if CONFIG_FEATURE_AUDIO_CODEC
    ESP_ERROR_CHECK(module_registry_add(audio_codec_module_descriptor()));
#endif
#if CONFIG_FEATURE_AUDIO_PIPELINE
    ESP_ERROR_CHECK(module_registry_add(audio_pipeline_module_descriptor()));
#endif
#if CONFIG_FEATURE_AUDIO_INPUT
    ESP_ERROR_CHECK(module_registry_add(audio_input_module_descriptor()));
#endif
#if CONFIG_FEATURE_VOICE_WAKE
    ESP_ERROR_CHECK(module_registry_add(voice_wake_module_descriptor()));
#endif
#if CONFIG_FEATURE_WAKE_FEEDBACK
    ESP_ERROR_CHECK(module_registry_add(wake_feedback_module_descriptor()));
#endif
#if CONFIG_FEATURE_BUTTON_INPUT
    ESP_ERROR_CHECK(module_registry_add(button_input_module_descriptor()));
#endif
#if CONFIG_FEATURE_LED_INDICATOR
    ESP_ERROR_CHECK(module_registry_add(led_indicator_module_descriptor()));
#endif
#if CONFIG_FEATURE_FACTORY_RESET
    ESP_ERROR_CHECK(module_registry_add(factory_reset_module_descriptor()));
#endif
#if CONFIG_FEATURE_VOLUME_CONTROL
    ESP_ERROR_CHECK(module_registry_add(volume_control_module_descriptor()));
#endif
#if CONFIG_FEATURE_AUDIO_OUTPUT
    ESP_ERROR_CHECK(module_registry_add(audio_output_module_descriptor()));
#endif
#if CONFIG_FEATURE_PARENT_POLICY
    ESP_ERROR_CHECK(module_registry_add(parent_policy_module_descriptor()));
#endif
#if CONFIG_FEATURE_PLAYBACK_QUEUE
    ESP_ERROR_CHECK(module_registry_add(playback_queue_module_descriptor()));
#endif
#if CONFIG_FEATURE_PROMPT_TONE
    ESP_ERROR_CHECK(module_registry_add(prompt_tone_module_descriptor()));
#endif
#if CONFIG_FEATURE_ERROR_RECOVERY
    ESP_ERROR_CHECK(module_registry_add(error_recovery_module_descriptor()));
#endif
#if CONFIG_FEATURE_UI_TEXT
    ESP_ERROR_CHECK(module_registry_add(ui_text_module_descriptor()));
#endif
}
#endif

static void start_voice_interaction(void) {
#if CONFIG_FEATURE_VOICE_WAKE
    const esp_err_t wake_result = voice_wake_start();
    if (wake_result != ESP_OK) {
        ESP_LOGE(TAG, "voice wake failed to start: %s",
                 esp_err_to_name(wake_result));
    }
#endif
#if CONFIG_FEATURE_BUTTON_INPUT && CONFIG_FEATURE_MODULE_REGISTRY
    const esp_err_t handler_result =
        button_input_set_event_handler(handle_button_gesture);
    if (handler_result != ESP_OK) {
        ESP_LOGE(TAG, "button handler registration failed: %s",
                 esp_err_to_name(handler_result));
    }
#endif
}

void app_main(void) {
#if CONFIG_FEATURE_MODULE_REGISTRY
    register_modules();

    const esp_err_t result = module_registry_initialize_all();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "one or more modules failed to initialize: %s",
                 esp_err_to_name(result));
    } else {
        start_voice_interaction();
    }
#elif CONFIG_FEATURE_SYSTEM_CORE
    ESP_ERROR_CHECK(system_core_init());
#endif
}
