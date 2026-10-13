set(vision_sync_sources "")

if(CONFIG_FEATURE_VISION_SYNC)
    list(APPEND vision_sync_sources
        "src/vision_sync.cpp"
        "src/vision_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${vision_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        camera_capture
        camera_vision
        device_binding_client
        device_identity
        module_registry
        network_manager
        time_sync
)

# Every request carries the device session token, so the shared transport
# policy is referenced whenever present and drops out otherwise.
if(CONFIG_FEATURE_VISION_SYNC AND CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
