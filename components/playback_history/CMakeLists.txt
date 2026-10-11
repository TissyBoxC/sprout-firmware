set(playback_history_sources "")

if(CONFIG_FEATURE_PLAYBACK_HISTORY)
    list(APPEND playback_history_sources
        "src/playback_history.c"
        "src/playback_history_core.c"
    )
endif()

idf_component_register(
    SRCS ${playback_history_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        content_library
        module_registry
)
