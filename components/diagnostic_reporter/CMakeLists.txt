set(diagnostic_reporter_sources "")

if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER)
    list(APPEND diagnostic_reporter_sources "src/diagnostic_reporter.cpp")
endif()

idf_component_register(
    SRCS ${diagnostic_reporter_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer freertos nvs_flash
    REQUIRES config_store error_recovery module_registry version_info
)
