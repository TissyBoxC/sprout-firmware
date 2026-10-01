set(version_info_sources "")

if(CONFIG_FEATURE_VERSION_INFO)
    list(APPEND version_info_sources "src/version_info.c")
endif()

idf_component_register(
    SRCS ${version_info_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_app_format module_registry
)
