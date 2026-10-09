set(privacy_guard_sources "")

if(CONFIG_FEATURE_PRIVACY_GUARD)
    list(APPEND privacy_guard_sources
        "src/privacy_guard.c"
        "src/privacy_guard_core.c"
    )
endif()

idf_component_register(
    SRCS ${privacy_guard_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_common
    REQUIRES config_store module_registry
)
