set(screen_report_sources "")

if(CONFIG_FEATURE_SCREEN_REPORT)
    list(APPEND screen_report_sources
        "src/screen_report.cpp"
        "src/screen_report_core.c"
    )
endif()

idf_component_register(
    SRCS ${screen_report_sources}
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
        screen_surface
        time_sync
)

# The request carries the device session token, so the shared transport policy
# is referenced whenever the component is built and the policy is present. The
# guard also keeps the empty (feature-disabled) component from becoming an
# INTERFACE library, which cannot take a private link dependency.
if(CONFIG_FEATURE_SCREEN_REPORT AND CONFIG_FEATURE_TRANSPORT_SECURITY AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
