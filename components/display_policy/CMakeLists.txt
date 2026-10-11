set(display_policy_sources "")

if(CONFIG_FEATURE_DISPLAY_POLICY)
    list(APPEND display_policy_sources
        "src/display_policy.c"
        "src/display_policy_core.c"
    )
endif()

idf_component_register(
    SRCS ${display_policy_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
