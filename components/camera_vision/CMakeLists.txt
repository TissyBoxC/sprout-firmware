set(camera_vision_sources "")

if(CONFIG_FEATURE_CAMERA_VISION)
    list(APPEND camera_vision_sources
        "src/camera_vision.c"
        "src/camera_vision_core.c"
    )
endif()

idf_component_register(
    SRCS ${camera_vision_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES esp_common
    REQUIRES camera_capture module_registry
)

# Vision results can carry child images, so the privacy guard is required
# whenever present. A profile that compiles every source out makes COMPONENT_LIB
# an INTERFACE target with no link step, so the dependency is gated on sources.
if(CONFIG_FEATURE_CAMERA_VISION)
    if(CONFIG_FEATURE_PRIVACY_GUARD AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../privacy_guard/CMakeLists.txt")
        idf_component_optional_requires(PRIVATE privacy_guard)
    endif()
endif()
