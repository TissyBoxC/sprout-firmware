set(wearable_location_sources "")

if(CONFIG_FEATURE_WEARABLE_LOCATION)
    list(APPEND wearable_location_sources
        "src/wearable_location.c"
        "src/wearable_core.c"
    )
endif()

idf_component_register(
    SRCS ${wearable_location_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
