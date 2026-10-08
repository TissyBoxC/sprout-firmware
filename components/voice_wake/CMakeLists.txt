set(voice_wake_sources "")

if(CONFIG_FEATURE_VOICE_WAKE)
    list(APPEND voice_wake_sources "src/voice_wake.c")
endif()

# audio_codec and module_registry appear in the public header. The wake engine
# owns the private capture task and only links ESP-SR when its backend is used.
set(voice_wake_private_requires audio_pipeline freertos esp_timer)

if(CONFIG_FEATURE_VOICE_WAKE AND CONFIG_VOICE_WAKE_BACKEND_ESP_SR)
    list(APPEND voice_wake_private_requires esp-sr)
endif()

idf_component_register(
    SRCS ${voice_wake_sources}
    INCLUDE_DIRS "include"
    REQUIRES audio_codec module_registry
    PRIV_REQUIRES ${voice_wake_private_requires}
)

# The optional dependency must be attached after idf_component_register(),
# because the helper links against COMPONENT_LIB, which only exists once the
# component library has been created.
if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER)
    idf_component_optional_requires(PRIVATE diagnostic_reporter)
endif()

if(CONFIG_FEATURE_PARENT_CONTROL_RUNTIME)
    idf_component_optional_requires(PRIVATE parent_control_runtime)
endif()
