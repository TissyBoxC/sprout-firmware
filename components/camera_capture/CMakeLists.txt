set(camera_capture_sources "")

if(CONFIG_FEATURE_CAMERA_CAPTURE)
    list(APPEND camera_capture_sources
        "src/camera_capture.c"
        "src/camera_capture_core.c"
    )
endif()

idf_component_register(
    SRCS ${camera_capture_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES esp_common
    REQUIRES device_capabilities module_registry
)

# Image upload is only possible with the privacy guard present. A profile that
# compiles every source out makes COMPONENT_LIB an INTERFACE target with no link
# step, so the optional dependency is only added when the component has sources.
if(CONFIG_FEATURE_CAMERA_CAPTURE)
    if(CONFIG_FEATURE_PRIVACY_GUARD AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../privacy_guard/CMakeLists.txt")
        idf_component_optional_requires(PRIVATE privacy_guard)
    endif()
endif()
