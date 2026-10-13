set(video_session_sources "")

if(CONFIG_FEATURE_VIDEO_SESSION)
    list(APPEND video_session_sources
        "src/video_session.c"
        "src/video_core.c"
        "src/video_session_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${video_session_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
