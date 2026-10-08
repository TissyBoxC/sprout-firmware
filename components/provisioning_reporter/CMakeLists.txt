set(provisioning_reporter_sources "")

if(CONFIG_FEATURE_PROVISIONING_REPORTER)
    list(APPEND provisioning_reporter_sources
        "src/provisioning_reporter.cpp"
        "src/provisioning_reporter_state.cpp"
    )
endif()

idf_component_register(
    SRCS ${provisioning_reporter_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos nvs_flash
    REQUIRES config_store module_registry version_info
)
