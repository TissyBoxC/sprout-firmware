set(camera_capture_sources "")

if(CONFIG_FEATURE_CAMERA_CAPTURE)
    list(APPEND camera_capture_sources
        "src/camera_capture.c"
        "src/camera_capture_core.c"
    )
    # Only the selected sensor model brings the driver glue in, so a profile
    # without a camera board never compiles it.
    if(CONFIG_CAMERA_SENSOR_OV2640)
        list(APPEND camera_capture_sources "src/camera_capture_sensor.cpp")
    endif()
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
    if(CONFIG_CAMERA_SENSOR_OV2640)
        # The driver arrives through idf_component.yml, so its headers are placed
        # on the include path by the component manager rather than by lib_deps.
        idf_component_optional_requires(PRIVATE esp32-camera)
    endif()
endif()

