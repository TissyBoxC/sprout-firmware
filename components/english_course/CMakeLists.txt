set(english_course_sources "")

if(CONFIG_FEATURE_ENGLISH_COURSE)
    list(APPEND english_course_sources
        "src/english_course.c"
        "src/english_course_core.c"
        "src/english_course_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${english_course_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
        freertos
        heap
    REQUIRES
        module_registry
)
