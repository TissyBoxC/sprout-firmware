set(time_sync_sources "")

if(CONFIG_FEATURE_TIME_SYNC)
    list(APPEND time_sync_sources
        "src/time_sync.c"
        "src/time_sync_pure.c"
    )
endif()

idf_component_register(
    SRCS ${time_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer lwip
    REQUIRES config_store module_registry network_manager
)
