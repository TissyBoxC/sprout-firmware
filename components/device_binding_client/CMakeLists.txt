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

# Transport security is always present in the shipped image; when it is removed
# the client keeps its explicit HTTPS prefix check and no shared helper is
# referenced.
if(CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
