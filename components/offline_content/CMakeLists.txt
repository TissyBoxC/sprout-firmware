set(offline_content_sources "")

if(CONFIG_FEATURE_OFFLINE_CONTENT)
    list(APPEND offline_content_sources
        "src/offline_content.c"
        "src/offline_content_core.c"
    )
endif()

idf_component_register(
    SRCS ${offline_content_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        module_registry
)
