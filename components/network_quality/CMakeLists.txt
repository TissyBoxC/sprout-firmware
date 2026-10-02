set(network_quality_sources "")

if(CONFIG_FEATURE_NETWORK_QUALITY)
    list(APPEND network_quality_sources "src/network_quality.c")
endif()

idf_component_register(
    SRCS ${network_quality_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_timer lwip
    REQUIRES module_registry network_manager
)
