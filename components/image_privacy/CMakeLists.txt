set(image_privacy_sources "")

if(CONFIG_FEATURE_IMAGE_PRIVACY)
    list(APPEND image_privacy_sources
        "src/image_privacy.c"
        "src/image_core.c"
    )
endif()

idf_component_register(
    SRCS ${image_privacy_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)
