set(bluetooth_audio_sources "")

if(CONFIG_FEATURE_BLUETOOTH_AUDIO)
    list(APPEND bluetooth_audio_sources
        "src/bluetooth_audio.c"
        "src/bluetooth_core.c"
    )
endif()

idf_component_register(
    SRCS ${bluetooth_audio_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
