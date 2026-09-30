# Keep the project application component explicit because PlatformIO uses
# `src` as its source directory rather than the ESP-IDF default `main`.
set(app_requires "")

if(CONFIG_FEATURE_UI_TEXT)
    list(APPEND app_requires "ui_text")
endif()

idf_component_register(
    SRCS "main.c"
    REQUIRES ${app_requires}
)
