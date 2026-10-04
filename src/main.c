#include "esp_err.h"
#include "esp_log.h"

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
        // A very-long press only arms the confirmed reset flow; erasing still
        // requires an explicit confirmation through factory_reset_confirm.
        (void)factory_reset_request(FACTORY_RESET_REASON_BUTTON_GESTURE);
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
