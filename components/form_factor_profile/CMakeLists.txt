set(form_factor_sources "")

if(CONFIG_FEATURE_FORM_FACTOR_PROFILE)
    list(APPEND form_factor_sources
        "src/form_factor_profile.c"
        "src/form_factor_core.c"
    )
endif()

idf_component_register(
    SRCS ${form_factor_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
