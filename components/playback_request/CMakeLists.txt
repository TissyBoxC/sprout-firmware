set(playback_request_sources "")

if(CONFIG_FEATURE_PLAYBACK_REQUEST)
    list(APPEND playback_request_sources
        "src/playback_request.c"
        "src/playback_request_core.c"
    )
endif()

idf_component_register(
    SRCS ${playback_request_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        module_registry
)
