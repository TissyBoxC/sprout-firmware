set(ui_text_sources "")

if(CONFIG_FEATURE_UI_TEXT)
    list(APPEND ui_text_sources
        "src/ui_text.c"
        "src/ui_text_store.c"
    )
endif()

idf_component_register(
    SRCS ${ui_text_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES config_store
    REQUIRES module_registry
)
