set(offline_policy_sync_sources "")

if(CONFIG_FEATURE_OFFLINE_POLICY_SYNC)
    list(APPEND offline_policy_sync_sources
        "src/offline_policy_sync.cpp"
    )
endif()

idf_component_register(
    SRCS ${offline_policy_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        device_binding_client
        device_identity
        module_registry
        network_manager
        offline_policy
        time_sync
)

# The policy request carries the device session token, so the shared transport
# policy is referenced whenever present and drops out otherwise.
if(CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
