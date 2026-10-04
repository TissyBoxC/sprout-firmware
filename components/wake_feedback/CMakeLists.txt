set(wake_feedback_sources "")

if(CONFIG_FEATURE_WAKE_FEEDBACK)
    list(APPEND wake_feedback_sources "src/wake_feedback.c")
endif()

idf_component_register(
    SRCS ${wake_feedback_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
    PRIV_REQUIRES esp_timer freertos
)

if(CONFIG_FEATURE_VOICE_WAKE)
    idf_component_optional_requires(PRIVATE voice_wake)
endif()

if(CONFIG_FEATURE_PROMPT_TONE)
    idf_component_optional_requires(PRIVATE prompt_tone)
endif()

if(CONFIG_FEATURE_LED_INDICATOR)
    idf_component_optional_requires(PRIVATE led_indicator)
endif()

if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER)
    idf_component_optional_requires(PRIVATE diagnostic_reporter)
endif()
