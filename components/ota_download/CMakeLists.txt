set(ota_download_sources "")

if(CONFIG_FEATURE_OTA_DOWNLOAD)
    list(APPEND ota_download_sources "src/ota_download.c")
endif()
if(CONFIG_FEATURE_OTA_DOWNLOAD OR CONFIG_FEATURE_OTA_MANAGER)
    list(APPEND ota_download_sources "src/ota_download_core.c")
endif()

idf_component_register(
    SRCS ${ota_download_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client esp-tls esp_timer mbedtls
    REQUIRES app_update esp_partition ota_validate
)
