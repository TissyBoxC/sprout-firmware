set(local_command_sources "")

if(CONFIG_FEATURE_LOCAL_COMMAND)
    list(APPEND local_command_sources
        "src/local_command.c"
        "src/local_command_core.c"
        "src/local_command_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${local_command_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
        freertos
    REQUIRES
        module_registry
)
