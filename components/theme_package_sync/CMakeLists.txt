set(theme_package_sync_sources "")

if(CONFIG_FEATURE_THEME_PACKAGE_SYNC)
    list(APPEND theme_package_sync_sources
        "src/theme_package_sync.cpp"
    )
endif()

idf_component_register(
    SRCS ${theme_package_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        content_library
        device_binding_client
        device_identity
        module_registry
        network_manager
        theme_package
        time_sync
)

# The theme request carries the device session token, so the shared transport
# policy is referenced whenever present and drops out otherwise.
if(CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
