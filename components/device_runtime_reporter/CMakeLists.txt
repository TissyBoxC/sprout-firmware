set(device_runtime_reporter_sources "")

if(CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER)
    list(APPEND device_runtime_reporter_sources "src/device_runtime_reporter.cpp")
endif()

set(device_runtime_reporter_requires
    cloud_auth
    config_store
    device_binding_client
    module_registry
    network_manager
    network_quality
    offline_fallback
    time_sync
)

# Keep the optional integration buildable when parent_policy is deleted. The
# source guard removes the calls at the same time.
if(CONFIG_FEATURE_PARENT_POLICY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../parent_policy/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "parent_policy")
endif()

# Diagnostics are optional and remain compatible with the original heartbeat
# contract when this component is removed.
if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../diagnostic_reporter/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "diagnostic_reporter")
endif()

idf_component_register(
    SRCS ${device_runtime_reporter_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_app_format esp_http_client esp-tls mbedtls
    REQUIRES ${device_runtime_reporter_requires}
)
