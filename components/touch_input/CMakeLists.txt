set(touch_input_sources "")

if(CONFIG_FEATURE_TOUCH_INPUT)
    list(APPEND touch_input_sources
        "src/touch_input.c"
        "src/touch_input_core.c"
    )
endif()

idf_component_register(
    SRCS ${touch_input_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
