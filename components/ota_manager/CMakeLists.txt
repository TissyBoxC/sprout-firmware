set(ota_manager_sources "")

if(CONFIG_FEATURE_OTA_MANAGER)
    list(APPEND ota_manager_sources
        "src/ota_manager_event_sequence.c"
        "src/ota_manager_state.c"
        "src/ota_manager.cpp"
    )
endif()

idf_component_register(
    SRCS ${ota_manager_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_app_format esp_http_client esp-tls esp_timer mbedtls
    REQUIRES cloud_auth config_store device_binding_client device_identity
             module_registry network_manager time_sync version_info
             app_update esp_partition ota_download ota_validate ota_rollback
)

if(CONFIG_FEATURE_LED_INDICATOR)
    idf_component_optional_requires(PRIVATE led_indicator)
endif()
