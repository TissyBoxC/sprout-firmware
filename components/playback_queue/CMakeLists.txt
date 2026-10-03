set(playback_queue_sources "")

if(CONFIG_FEATURE_PLAYBACK_QUEUE)
    list(APPEND playback_queue_sources "src/playback_queue.c")
endif()

idf_component_register(
    SRCS ${playback_queue_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2s freertos heap
    REQUIRES
        audio_codec
        audio_pipeline
        module_registry
        volume_control
)
