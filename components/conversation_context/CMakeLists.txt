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
