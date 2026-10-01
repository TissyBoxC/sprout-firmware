set(error_recovery_sources "")

if(CONFIG_FEATURE_ERROR_RECOVERY)
    list(APPEND error_recovery_sources "src/error_recovery.c")
endif()

idf_component_register(
    SRCS ${error_recovery_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_system freertos module_registry
)
