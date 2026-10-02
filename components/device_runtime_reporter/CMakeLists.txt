set(device_runtime_reporter_sources "")

if(CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER)
    list(APPEND device_runtime_reporter_sources "src/device_runtime_reporter.cpp")
endif()

idf_component_register(
    SRCS ${device_runtime_reporter_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_app_format esp_http_client esp-tls mbedtls
    REQUIRES cloud_auth config_store device_binding_client module_registry network_manager network_quality offline_fallback time_sync
)
