set(usage_ledger_sources "")

if(CONFIG_FEATURE_USAGE_LEDGER)
    list(APPEND usage_ledger_sources
        "src/usage_ledger.c"
        "src/usage_ledger_core.c"
    )
endif()

idf_component_register(
    SRCS ${usage_ledger_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer
    REQUIRES config_store module_registry time_sync
)
