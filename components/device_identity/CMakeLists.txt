set(device_identity_sources "")

if(CONFIG_FEATURE_DEVICE_IDENTITY)
    list(APPEND device_identity_sources "src/device_identity.c")
endif()

idf_component_register(
    SRCS ${device_identity_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_hw_support mbedtls module_registry
)
