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
    REQUIRES device_capabilities module_registry privacy_guard
)
