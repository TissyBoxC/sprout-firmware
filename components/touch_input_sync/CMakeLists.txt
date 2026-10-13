set(touch_input_sync_sources "")

if(CONFIG_FEATURE_TOUCH_INPUT_SYNC)
    list(APPEND touch_input_sync_sources
        "src/touch_input_sync.cpp"
        "src/touch_input_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${touch_input_sync_sources}
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
        network_manager
        time_sync
        touch_input
)

if(CONFIG_FEATURE_TOUCH_INPUT_SYNC AND CONFIG_FEATURE_TRANSPORT_SECURITY AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
