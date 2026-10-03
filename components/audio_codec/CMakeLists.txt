set(audio_codec_sources "")

if(CONFIG_FEATURE_AUDIO_CODEC)
    list(APPEND audio_codec_sources "src/audio_codec.c")
endif()

# The Opus encoder and decoder live in the managed esp_audio_codec component.
# When the feature is disabled both the source and the dependency drop out, so a
# profile without audio links no codec code at all.
set(audio_codec_requires module_registry)

if(CONFIG_FEATURE_AUDIO_CODEC)
    list(APPEND audio_codec_requires esp_audio_codec)
endif()

idf_component_register(
    SRCS ${audio_codec_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos
    REQUIRES ${audio_codec_requires}
)
