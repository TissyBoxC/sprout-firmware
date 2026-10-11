set(cellular_sources "")

if(CONFIG_FEATURE_CELLULAR_4G)
    list(APPEND cellular_sources
        "src/cellular_4g.c"
        "src/cellular_core.c"
    )
endif()

idf_component_register(
    SRCS ${cellular_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
