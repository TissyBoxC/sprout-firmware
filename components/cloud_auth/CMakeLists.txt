set(cloud_auth_sources "")

if(CONFIG_FEATURE_CLOUD_AUTH)
    list(APPEND cloud_auth_sources "src/cloud_auth.c")
endif()

idf_component_register(
    SRCS ${cloud_auth_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer
    REQUIRES device_binding_client module_registry network_manager time_sync
)
