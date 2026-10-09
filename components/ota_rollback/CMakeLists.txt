set(ota_rollback_sources "")

if(CONFIG_FEATURE_OTA_ROLLBACK)
    list(APPEND ota_rollback_sources
        "src/ota_rollback_policy.c"
        "src/ota_rollback.c"
    )
endif()

idf_component_register(
    SRCS ${ota_rollback_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_update
    REQUIRES config_store module_registry ota_validate
)
