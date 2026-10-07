set(voice_session_sources "")

if(CONFIG_FEATURE_VOICE_SESSION)
    list(APPEND voice_session_sources
        "src/voice_session.c"
        "src/voice_session_protocol.c"
    )
endif()

# Local components must be listed unconditionally in the register() call.
# Conditional list(APPEND ...) entries here are not reflected in the
# generated project_description.json, so include paths would be missing even
# though the sources are compiled. Only the sources stay gated above.
idf_component_register(
    SRCS ${voice_session_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        audio_codec
        module_registry
    PRIV_REQUIRES
        esp_timer
        freertos
        audio_input
        audio_pipeline
        cloud_auth
        device_binding_client
        device_identity
        network_manager
        playback_queue
        espressif__esp_websocket_client
        espressif__cjson
)

# A minimal profile compiles every source out, which makes COMPONENT_LIB an
# INTERFACE target. idf_component_optional_requires() only supports targets
# with a link step, so guard the whole optional block on the component having
# sources and attach each dependency behind its own feature flag.
if(CONFIG_FEATURE_VOICE_SESSION)
    if(CONFIG_FEATURE_VOICE_WAKE)
        idf_component_optional_requires(PRIVATE voice_wake)
    endif()

    if(CONFIG_FEATURE_VERSION_INFO)
        idf_component_optional_requires(PRIVATE version_info)
    endif()

    if(CONFIG_FEATURE_DEVICE_CAPABILITIES)
        idf_component_optional_requires(PRIVATE device_capabilities)
    endif()

    if(CONFIG_FEATURE_CONVERSATION_CONTEXT)
        idf_component_optional_requires(PRIVATE conversation_context)
    endif()

    if(CONFIG_FEATURE_CHILD_PROMPT_PROFILE)
        idf_component_optional_requires(PRIVATE child_prompt_profile)
    endif()
endif()
