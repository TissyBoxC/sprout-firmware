set(ui_text_sources "")

if(CONFIG_FEATURE_UI_TEXT)
    list(APPEND ui_text_sources "src/ui_text.c")
endif()

idf_component_register(
    SRCS ${ui_text_sources}
    INCLUDE_DIRS "include"
)
