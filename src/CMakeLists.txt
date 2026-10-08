# Keep the project application component explicit because PlatformIO uses
# `src` as its source directory rather than the ESP-IDF default `main`.
set(app_requires "")

if(CONFIG_FEATURE_ERROR_CODE)
    list(APPEND app_requires "error_code")
endif()

if(CONFIG_FEATURE_DEVICE_IDENTITY)
    list(APPEND app_requires "device_identity")
endif()

if(CONFIG_FEATURE_DEVICE_CAPABILITIES)
    list(APPEND app_requires "device_capabilities")
endif()

if(CONFIG_FEATURE_AUDIO_CODEC)
    list(APPEND app_requires "audio_codec")
endif()

if(CONFIG_FEATURE_AUDIO_PIPELINE)
    list(APPEND app_requires "audio_pipeline")
endif()

if(CONFIG_FEATURE_AUDIO_INPUT)
    list(APPEND app_requires "audio_input")
endif()

if(CONFIG_FEATURE_VOICE_WAKE)
    list(APPEND app_requires "voice_wake")
endif()

if(CONFIG_FEATURE_WAKE_FEEDBACK)
    list(APPEND app_requires "wake_feedback")
endif()

if(CONFIG_FEATURE_BUTTON_INPUT)
    list(APPEND app_requires "button_input")
endif()

if(CONFIG_FEATURE_LED_INDICATOR)
    list(APPEND app_requires "led_indicator")
endif()

if(CONFIG_FEATURE_FACTORY_RESET)
    list(APPEND app_requires "factory_reset")
endif()

if(CONFIG_FEATURE_VOLUME_CONTROL)
    list(APPEND app_requires "volume_control")
endif()

if(CONFIG_FEATURE_AUDIO_OUTPUT)
    list(APPEND app_requires "audio_output")
endif()

if(CONFIG_FEATURE_PLAYBACK_QUEUE)
    list(APPEND app_requires "playback_queue")
endif()

if(CONFIG_FEATURE_PROMPT_TONE)
    list(APPEND app_requires "prompt_tone")
endif()

if(CONFIG_FEATURE_DEVICE_PROVISIONING)
    list(APPEND app_requires "device_provisioning")
endif()

if(CONFIG_FEATURE_CLOUD_AUTH)
    list(APPEND app_requires "cloud_auth")
endif()

if(CONFIG_FEATURE_PARENT_POLICY)
    list(APPEND app_requires "parent_policy")
endif()

if(CONFIG_FEATURE_USAGE_LEDGER)
    list(APPEND app_requires "usage_ledger")
endif()

if(CONFIG_FEATURE_PARENT_CONTROL_RUNTIME)
    list(APPEND app_requires "parent_control_runtime")
endif()

if(CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER)
    list(APPEND app_requires "device_runtime_reporter")
endif()

if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER)
    list(APPEND app_requires "diagnostic_reporter")
endif()

if(CONFIG_FEATURE_PROVISIONING_REPORTER)
    list(APPEND app_requires "provisioning_reporter")
endif()

if(CONFIG_FEATURE_ERROR_RECOVERY)
    list(APPEND app_requires "error_recovery")
endif()

if(CONFIG_FEATURE_MODULE_REGISTRY)
    list(APPEND app_requires "module_registry")
endif()

if(CONFIG_FEATURE_SYSTEM_CORE)
    list(APPEND app_requires "system_core")
endif()

if(CONFIG_FEATURE_TIME_SYNC)
    list(APPEND app_requires "time_sync")
endif()

if(CONFIG_FEATURE_NETWORK_QUALITY)
    list(APPEND app_requires "network_quality")
endif()

if(CONFIG_FEATURE_OFFLINE_FALLBACK)
    list(APPEND app_requires "offline_fallback")
endif()

if(CONFIG_FEATURE_UI_TEXT)
    list(APPEND app_requires "ui_text")
endif()

if(CONFIG_FEATURE_VERSION_INFO)
    list(APPEND app_requires "version_info")
endif()

if(CONFIG_FEATURE_CONVERSATION_CONTEXT)
    list(APPEND app_requires "conversation_context")
endif()

if(CONFIG_FEATURE_CHILD_PROMPT_PROFILE)
    list(APPEND app_requires "child_prompt_profile")
endif()

if(CONFIG_FEATURE_VOICE_SESSION)
    list(APPEND app_requires "voice_session")
endif()

if(CONFIG_FEATURE_CONTENT_LIBRARY)
    list(APPEND app_requires "content_library")
endif()

if(CONFIG_FEATURE_CONTENT_DOWNLOADER)
    list(APPEND app_requires "content_downloader")
endif()

if(CONFIG_FEATURE_CONTENT_PACKAGE_MANAGER)
    list(APPEND app_requires "content_package_manager")
endif()

idf_component_register(
    SRCS "main.c"
    REQUIRES ${app_requires}
)
