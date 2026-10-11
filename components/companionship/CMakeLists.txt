set(companionship_sources "")

if(CONFIG_FEATURE_COMPANIONSHIP)
    list(APPEND companionship_sources
        "src/companionship.c"
        "src/companionship_core.c"
    )
endif()

idf_component_register(
    SRCS ${companionship_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
)
