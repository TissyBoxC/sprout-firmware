set(module_registry_sources "")

if(CONFIG_FEATURE_MODULE_REGISTRY)
    list(APPEND module_registry_sources "src/module_registry.c")
endif()

idf_component_register(
    SRCS ${module_registry_sources}
    INCLUDE_DIRS "include"
)
