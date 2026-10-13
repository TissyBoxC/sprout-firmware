set(connectivity_report_sources "")

if(CONFIG_FEATURE_CONNECTIVITY_REPORT)
    list(APPEND connectivity_report_sources
        "src/connectivity_report.cpp"
        "src/connectivity_report_core.c"
    )
endif()

idf_component_register(
    SRCS ${connectivity_report_sources}
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
        network_quality
        time_sync
)

# The request carries the device session token, so the shared transport policy
# is referenced whenever the component is built and the policy is present. The
# guard also keeps the empty (feature-disabled) component from becoming an
# INTERFACE library, which cannot take a private link dependency.
if(CONFIG_FEATURE_CONNECTIVITY_REPORT AND CONFIG_FEATURE_TRANSPORT_SECURITY AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()

# When the cellular policy store is linked, the reporter reports the revision
# the connectivity layer actually applied instead of a hardcoded zero. A
# Wi-Fi-only image never links cellular_4g and reports revision zero, which is
# the truthful "no cellular policy applied" value for that build.
if(CONFIG_FEATURE_CONNECTIVITY_REPORT AND CONFIG_FEATURE_CELLULAR_4G AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../cellular_4g/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE cellular_4g)
endif()

# On a Wi-Fi-only image the reporter reads the revision persisted by
# connectivity_policy_sync so the platform's up-to-date check converges instead
# of always seeing zero.
if(CONFIG_FEATURE_CONNECTIVITY_POLICY_SYNC AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../connectivity_policy_sync/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE connectivity_policy_sync)
endif()
