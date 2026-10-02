set(provisioning_payload_sources "")

if(CONFIG_FEATURE_PROVISIONING_PAYLOAD)
    list(APPEND provisioning_payload_sources "src/provisioning_payload.c")
endif()

idf_component_register(
    SRCS ${provisioning_payload_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
)
