set(display_report_sources "")

if(CONFIG_FEATURE_DISPLAY_REPORT)
    list(APPEND display_report_sources
        "src/display_report.cpp"
        "src/display_report_core.c"
    )
endif()

idf_component_register(
    SRCS ${display_report_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        device_binding_client
        device_capabilities
        device_identity
        display_policy
        module_registry
        network_manager
        time_sync
)

# The request carries the device session token, so the shared transport policy
# is referenced whenever the component is built and the policy is present. The
# guard also keeps the empty (feature-disabled) component from becoming an
# INTERFACE library, which cannot take a private link dependency.
if(CONFIG_FEATURE_DISPLAY_REPORT AND CONFIG_FEATURE_TRANSPORT_SECURITY AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
