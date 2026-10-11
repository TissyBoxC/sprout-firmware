set(growth_record_sources "")

if(CONFIG_FEATURE_GROWTH_RECORD)
    list(APPEND growth_record_sources
        "src/growth_record.c"
        "src/growth_record_core.c"
    )
endif()

idf_component_register(
    SRCS ${growth_record_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        module_registry
)
