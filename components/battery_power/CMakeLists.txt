set(battery_sources "")

if(CONFIG_FEATURE_BATTERY_POWER)
    list(APPEND battery_sources
        "src/battery_power.c"
        "src/battery_core.c"
    )
endif()

idf_component_register(
    SRCS ${battery_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
