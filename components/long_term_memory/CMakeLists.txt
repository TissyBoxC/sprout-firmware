set(long_term_memory_sources "")

if(CONFIG_FEATURE_LONG_TERM_MEMORY)
    list(APPEND long_term_memory_sources
        "src/long_term_memory.c"
        "src/long_term_memory_core.c"
    )
endif()

idf_component_register(
    SRCS ${long_term_memory_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        module_registry
)
