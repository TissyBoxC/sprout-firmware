set(security_posture_sources "")

if(CONFIG_FEATURE_SECURITY_POSTURE)
    list(APPEND security_posture_sources
        "src/security_posture.c"
        "src/security_posture_core.c"
    )
endif()

idf_component_register(
    SRCS ${security_posture_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
    PRIV_REQUIRES bootloader_support efuse esp_common
)
