set(network_manager_sources "")

if(CONFIG_FEATURE_NETWORK_MANAGER)
    list(APPEND network_manager_sources "src/network_manager.c")
endif()

idf_component_register(
    SRCS ${network_manager_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_event esp_netif esp_wifi nvs_flash
    REQUIRES config_store module_registry
)
