set(audio_output_sources "")

if(CONFIG_FEATURE_AUDIO_OUTPUT)
    list(APPEND audio_output_sources "src/audio_output.c")
endif()

# audio_pipeline owns the final I2S write, so it stays private to the module.
idf_component_register(
    SRCS ${audio_output_sources}
    INCLUDE_DIRS "include"
    REQUIRES audio_codec module_registry
    PRIV_REQUIRES audio_pipeline freertos heap
)

if(CONFIG_FEATURE_VOLUME_CONTROL)
    idf_component_optional_requires(PRIVATE volume_control)
endif()
