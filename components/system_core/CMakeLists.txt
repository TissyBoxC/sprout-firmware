set(system_core_sources "")

if(CONFIG_FEATURE_SYSTEM_CORE)
    list(APPEND system_core_sources "src/system_core.c")
endif()

idf_component_register(
    SRCS ${system_core_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_event module_registry
)
