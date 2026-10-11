set(family_linkage_sources "")

if(CONFIG_FEATURE_FAMILY_LINKAGE)
    list(APPEND family_linkage_sources
        "src/family_linkage.c"
        "src/family_core.c"
    )
endif()

idf_component_register(
    SRCS ${family_linkage_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
