set(motion_sources "")

if(CONFIG_FEATURE_MOTION_CONTROL)
    list(APPEND motion_sources
        "src/motion_control.c"
        "src/motion_core.c"
    )
endif()

idf_component_register(
    SRCS ${motion_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
