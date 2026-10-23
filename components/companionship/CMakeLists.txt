set(companionship_sources "")

if(CONFIG_FEATURE_COMPANIONSHIP)
    list(APPEND companionship_sources
        "src/companionship.c"
        "src/companionship_core.c"
    )
endif()

idf_component_register(
    SRCS ${companionship_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
)

# Companionship is the moment an emotion is decided, so it is where the reporter
# is fed. The dependency stays optional and the call is compiled out with
# FEATURE_EMOTION_REPORT, so companionship keeps working as a pure local
# fallback on a build without the reporter.
if(CONFIG_FEATURE_COMPANIONSHIP
        AND CONFIG_FEATURE_EMOTION_REPORT
        AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../emotion_report/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE emotion_report)
endif()
