set(config_store_sources "")

if(CONFIG_FEATURE_CONFIG_STORE)
    list(APPEND config_store_sources "src/config_store.c")
endif()

idf_component_register(
    SRCS ${config_store_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash
    REQUIRES module_registry
)
