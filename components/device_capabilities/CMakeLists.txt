set(device_capabilities_sources "")

if(CONFIG_FEATURE_DEVICE_CAPABILITIES)
    list(APPEND device_capabilities_sources "src/device_capabilities.c")
endif()

idf_component_register(
    SRCS ${device_capabilities_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
)
