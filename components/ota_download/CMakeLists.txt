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
    REQUIRES app_update esp_partition ota_validate ed25519_verify
)

# An OTA image must come from a trusted host. The shared policy check is wired
# in whenever transport_security is present and drops out in minimal builds.
if(CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
