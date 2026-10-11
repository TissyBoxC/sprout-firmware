set(screen_surface_sources "")

if(CONFIG_FEATURE_SCREEN_SURFACE)
    list(APPEND screen_surface_sources
        "src/screen_surface.c"
        "src/screen_core.c"
    )
endif()

idf_component_register(
    SRCS ${screen_surface_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
