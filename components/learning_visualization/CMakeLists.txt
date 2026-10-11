set(learning_visualization_sources "")

if(CONFIG_FEATURE_LEARNING_VISUALIZATION)
    list(APPEND learning_visualization_sources
        "src/learning_visualization.c"
        "src/learning_core.c"
    )
endif()

idf_component_register(
    SRCS ${learning_visualization_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
