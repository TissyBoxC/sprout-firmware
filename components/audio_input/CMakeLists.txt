set(audio_input_sources "")

if(CONFIG_FEATURE_AUDIO_INPUT)
    list(APPEND audio_input_sources
        "src/audio_input.c"
        "src/audio_input_dsp.c"
    )
endif()

# audio_codec and module_registry appear in the public header, so they are
# normal requirements. audio_pipeline is only used by the implementation, so it
# stays private and a build without the capture path links no I2S code here.
idf_component_register(
    SRCS ${audio_input_sources}
    INCLUDE_DIRS "include"
    REQUIRES audio_codec module_registry
    PRIV_REQUIRES audio_pipeline freertos esp_timer
)
