set(content_package_manager_sources "")

if(CONFIG_FEATURE_CONTENT_PACKAGE_MANAGER)
    list(APPEND content_package_manager_sources
        "src/content_package_manager.cpp"
        "src/content_package_state.c"
    )
endif()

idf_component_register(
    SRCS ${content_package_manager_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        config_store
        content_downloader
        content_library
        device_binding_client
        device_identity
        module_registry
        network_manager
        time_sync
)
