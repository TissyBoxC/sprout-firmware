set(voice_intercom_sources "")

if(CONFIG_FEATURE_VOICE_INTERCOM)
    list(APPEND voice_intercom_sources
        "src/voice_intercom.cpp"
        "src/voice_intercom_core.c"
    )
endif()

# Local components must be listed unconditionally in the register() call.
# Conditional list(APPEND ...) entries here are not reflected in the generated
# project_description.json, so include paths would be missing even though the
# sources are compiled. Only the sources stay gated above.
#
# voice_intercom_core.h is exported alongside the public header because the
# phase, end-reason and snapshot types are declared there, and the composition
# root includes it through voice_intercom.h.
idf_component_register(
    SRCS ${voice_intercom_sources}
    INCLUDE_DIRS "include" "src"
    REQUIRES
        button_input
        device_message
        module_registry
        voice_session
    PRIV_REQUIRES
        audio_input
        audio_output
        audio_pipeline
        config_store
        device_binding_client
        device_identity
        espressif__mqtt
        freertos
        led_indicator
        network_manager
        prompt_tone
)

# Calling control travels over the mutual-TLS device command channel and the
# media path is shared with the realtime voice session, so the module has no
# optional dependencies of its own beyond the consent guard below. With the
# switch off the component registers as an ESP-IDF INTERFACE (config-only)
# target, and idf_component_optional_requires would try a PRIVATE link
# library on it, which CMake rejects, so every optional edge is gated on the
# same switch that produces sources.
if(CONFIG_FEATURE_VOICE_INTERCOM)
    if(CONFIG_FEATURE_PRIVACY_GUARD AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../privacy_guard/CMakeLists.txt")
        idf_component_optional_requires(PRIVATE privacy_guard)
    endif()
endif()
