set(favorites_sources "")

if(CONFIG_FEATURE_FAVORITES)
    list(APPEND favorites_sources
        "src/favorites.c"
        "src/favorites_core.c"
        "src/favorites_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${favorites_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
        freertos
    REQUIRES
        content_library
        module_registry
)
