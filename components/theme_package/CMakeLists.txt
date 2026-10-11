set(theme_package_sources "")

if(CONFIG_FEATURE_THEME_PACKAGE)
    list(APPEND theme_package_sources
        "src/theme_package.c"
        "src/theme_package_core.c"
    )
endif()

idf_component_register(
    SRCS ${theme_package_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        content_library
        module_registry
)
