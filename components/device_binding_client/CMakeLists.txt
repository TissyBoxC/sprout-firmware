set(device_binding_client_sources "")

if(CONFIG_FEATURE_DEVICE_BINDING_CLIENT)
    list(APPEND device_binding_client_sources "src/device_binding_client.cpp")
endif()

idf_component_register(
    SRCS ${device_binding_client_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_app_format esp_http_client esp-tls mbedtls
    REQUIRES config_store device_capabilities device_identity module_registry
)
