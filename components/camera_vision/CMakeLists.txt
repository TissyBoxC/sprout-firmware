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
    REQUIRES camera_capture module_registry privacy_guard
)
