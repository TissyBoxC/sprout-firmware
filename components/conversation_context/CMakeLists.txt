set(conversation_context_sources "")

if(CONFIG_FEATURE_CONVERSATION_CONTEXT)
    list(APPEND conversation_context_sources
        "src/conversation_context.c"
        "src/conversation_context_ring.c"
    )
endif()

idf_component_register(
    SRCS ${conversation_context_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer freertos
    REQUIRES module_registry
)

# The recent-turn window stores child conversation summaries, so it consults
# the privacy guard before retaining a turn. A minimal profile compiles every
# source out, which makes COMPONENT_LIB an INTERFACE target that has no link
# step, so the optional dependency is gated on the component having sources.
if(CONFIG_FEATURE_CONVERSATION_CONTEXT)
    if(CONFIG_FEATURE_PRIVACY_GUARD AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../privacy_guard/CMakeLists.txt")
        idf_component_optional_requires(PRIVATE privacy_guard)
    endif()
endif()
