set(audio_pipeline_sources "")

if(CONFIG_FEATURE_AUDIO_PIPELINE)
    list(APPEND audio_pipeline_sources "src/audio_pipeline.c")
endif()

idf_component_register(
    SRCS ${audio_pipeline_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2s freertos
    REQUIRES module_registry audio_codec
)
