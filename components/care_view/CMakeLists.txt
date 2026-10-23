set(care_view_sources "")

if(CONFIG_FEATURE_CARE_VIEW)
    list(APPEND care_view_sources
        "src/care_view.cpp"
        "src/care_view_core.c"
    )
endif()

# Local components must be listed unconditionally in the register() call.
# Conditional list(APPEND ...) entries are not reflected in the generated
# project_description.json, so include paths would be missing even though the
# sources are compiled. Only the sources stay gated above.
#
# care_view_core.h is exported alongside the public header because the phase,
# end-reason and snapshot types are declared there and the composition root
# includes it through care_view.h.
idf_component_register(
    SRCS ${care_view_sources}
    INCLUDE_DIRS "include" "src"
    REQUIRES
        module_registry
    PRIV_REQUIRES
        device_binding_client
        device_identity
        esp_http_client
        esp_timer
        freertos
        espressif__mqtt
        mbedtls
)

# Every capability this module uses is itself removable, so each edge is declared
# optional: with care_view off and one of them removed, the component still
# configures instead of failing at the CMake step, and the module keeps building
# when the capability is present.
if(CONFIG_FEATURE_CARE_VIEW)
    foreach(_care_view_dependency
            audio_output
            button_input
            camera_capture
            config_store
            device_identity
            device_message
            espressif__mqtt
            led_indicator
            prompt_tone
            screen_surface)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../${_care_view_dependency}/CMakeLists.txt")
            idf_component_optional_requires(PRIVATE ${_care_view_dependency})
        endif()
    endforeach()

    if(CONFIG_FEATURE_PRIVACY_GUARD AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../privacy_guard/CMakeLists.txt")
        idf_component_optional_requires(PRIVATE privacy_guard)
    endif()
endif()
