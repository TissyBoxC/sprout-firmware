set(temperature_monitor_sources "")

if(CONFIG_FEATURE_TEMPERATURE_MONITOR)
    list(APPEND temperature_monitor_sources "src/temperature_monitor.c")
    list(APPEND temperature_monitor_sources "src/temperature_monitor_core.c")
endif()

idf_component_register(
    SRCS ${temperature_monitor_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES module_registry
    PRIV_REQUIRES esp_driver_tsens esp_timer
)
