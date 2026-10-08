set(parent_control_runtime_sources "")

if(CONFIG_FEATURE_PARENT_CONTROL_RUNTIME)
    list(APPEND parent_control_runtime_sources
        "src/parent_control_runtime.c"
        "src/parent_control_decision.c"
    )
endif()

idf_component_register(
    SRCS ${parent_control_runtime_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer
    REQUIRES
        module_registry
        parent_policy
        time_sync
        usage_ledger
)
