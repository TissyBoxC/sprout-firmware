set(device_identity_sources "")

if(CONFIG_FEATURE_DEVICE_IDENTITY)
    list(APPEND device_identity_sources "src/device_identity.c")
endif()

idf_component_register(
    SRCS ${device_identity_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hw_support mbedtls nvs_flash
    REQUIRES module_registry
)
