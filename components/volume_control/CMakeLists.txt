set(volume_control_sources "")

if(CONFIG_FEATURE_VOLUME_CONTROL)
    list(APPEND volume_control_sources "src/volume_control.c")
endif()

idf_component_register(
    SRCS ${volume_control_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos
    REQUIRES
        config_store
        module_registry
)
