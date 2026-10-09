set(content_filter_sources "")

if(CONFIG_FEATURE_CONTENT_FILTER)
    list(APPEND content_filter_sources
        "src/content_filter.c"
        "src/content_filter_core.c"
    )
endif()

idf_component_register(
    SRCS ${content_filter_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_common
    REQUIRES module_registry
)
