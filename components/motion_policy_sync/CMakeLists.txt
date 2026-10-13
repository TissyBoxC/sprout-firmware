set(motion_policy_sync_sources "")

if(CONFIG_FEATURE_MOTION_POLICY_SYNC)
    list(APPEND motion_policy_sync_sources
        "src/motion_policy_sync.cpp"
        "src/motion_policy_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${motion_policy_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        device_binding_client
        device_identity
        module_registry
        motion_control
        network_manager
        time_sync
)

if(CONFIG_FEATURE_MOTION_POLICY_SYNC AND CONFIG_FEATURE_TRANSPORT_SECURITY AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
