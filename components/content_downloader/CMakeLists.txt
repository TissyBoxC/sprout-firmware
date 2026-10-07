set(content_downloader_sources "")

if(CONFIG_FEATURE_CONTENT_DOWNLOADER)
    list(APPEND content_downloader_sources
        "src/content_downloader.c"
        "src/content_download_state.c"
        "src/content_playback_chunker.c"
        "src/content_playback.c"
    )
endif()

idf_component_register(
    SRCS ${content_downloader_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_client
        esp_partition
        esp_timer
        mbedtls
        spiffs
    REQUIRES
        content_library
        module_registry
        network_manager
        playback_queue
)
