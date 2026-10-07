set(content_library_sources "")

if(CONFIG_FEATURE_CONTENT_LIBRARY)
    list(APPEND content_library_sources
        "src/content_library.c"
        "src/content_library_index.c"
    )
endif()

idf_component_register(
    SRCS ${content_library_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES config_store esp_partition
    REQUIRES module_registry
)
