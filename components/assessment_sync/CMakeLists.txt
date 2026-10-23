set(assessment_sync_sources "")

if(CONFIG_FEATURE_ASSESSMENT_SYNC)
    list(APPEND assessment_sync_sources
        "src/assessment_sync.cpp"
        "src/assessment_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${assessment_sync_sources}
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
        time_sync
)

# The session request carries the device session token, so the shared
# transport policy is referenced whenever present and drops out otherwise.
# The dependency is only added when this component owns sources: with the
# feature disabled the component registers as an ESP-IDF INTERFACE
# (config-only) target, and idf_component_optional_requires would try
# target_link_libraries(... PRIVATE ...) on it, which CMake rejects.
if(CONFIG_FEATURE_ASSESSMENT_SYNC
        AND CONFIG_FEATURE_TRANSPORT_SECURITY
        AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
