set(device_provisioning_sources "")

if(CONFIG_FEATURE_DEVICE_PROVISIONING)
    list(APPEND device_provisioning_sources "src/device_provisioning.cpp")
endif()

idf_component_register(
    SRCS ${device_provisioning_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt esp_event esp_timer
    REQUIRES config_store device_binding_client device_identity module_registry network_manager provisioning_payload
)
