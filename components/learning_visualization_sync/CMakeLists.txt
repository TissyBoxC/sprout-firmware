set(learning_visualization_sync_sources "")

if(CONFIG_FEATURE_LEARNING_VISUALIZATION_SYNC)
    list(APPEND learning_visualization_sync_sources
        "src/learning_visualization_sync.cpp"
    )
endif()

idf_component_register(
    SRCS ${learning_visualization_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        device_binding_client
        device_identity
        learning_visualization
        module_registry
        network_manager
        time_sync
)

# The policy request carries the device session token, so the shared transport
# policy is referenced whenever present and drops out otherwise.
if(CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
