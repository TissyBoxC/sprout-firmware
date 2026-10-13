set(offline_policy_sources "")

if(CONFIG_FEATURE_OFFLINE_POLICY)
    list(APPEND offline_policy_sources
        "src/offline_policy.c"
        "src/offline_policy_core.c"
    )
endif()

idf_component_register(
    SRCS ${offline_policy_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
        freertos
    REQUIRES
        module_registry
)
