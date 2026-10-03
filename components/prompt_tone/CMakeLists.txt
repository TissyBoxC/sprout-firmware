set(prompt_tone_sources "")

if(CONFIG_FEATURE_PROMPT_TONE)
    list(APPEND prompt_tone_sources "src/prompt_tone.c")
endif()

idf_component_register(
    SRCS ${prompt_tone_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos
    REQUIRES
        audio_codec
        audio_pipeline
        module_registry
        playback_queue
)
