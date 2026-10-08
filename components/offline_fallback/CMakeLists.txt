set(offline_fallback_sources "")

if(CONFIG_FEATURE_OFFLINE_FALLBACK)
    list(APPEND offline_fallback_sources "src/offline_fallback.c")
endif()

idf_component_register(
    SRCS ${offline_fallback_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer freertos
    REQUIRES config_store module_registry network_manager time_sync
)
