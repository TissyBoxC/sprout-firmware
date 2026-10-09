set(ota_validate_sources "")

if(CONFIG_FEATURE_OTA_VALIDATE)
    list(APPEND ota_validate_sources
        "src/ota_manifest.c"
        "src/ota_validate.c"
    )
endif()

idf_component_register(
    SRCS ${ota_validate_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_app_format bootloader_support
    REQUIRES app_update esp_partition
)
