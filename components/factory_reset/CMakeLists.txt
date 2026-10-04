set(factory_reset_sources "")

if(CONFIG_FEATURE_FACTORY_RESET)
    list(APPEND factory_reset_sources "src/factory_reset.c")
endif()

idf_component_register(
    SRCS ${factory_reset_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer freertos nvs_flash
    REQUIRES module_registry
)

if(CONFIG_FEATURE_CONFIG_STORE)
    idf_component_optional_requires(PRIVATE config_store)
endif()

if(CONFIG_FEATURE_BUTTON_INPUT)
    idf_component_optional_requires(PRIVATE button_input)
endif()

if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER)
    idf_component_optional_requires(PRIVATE diagnostic_reporter)
endif()
