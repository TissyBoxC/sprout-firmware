set(button_input_sources "")

if(CONFIG_FEATURE_BUTTON_INPUT)
    list(APPEND button_input_sources "src/button_input.c")
endif()

idf_component_register(
    SRCS ${button_input_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
    PRIV_REQUIRES esp_driver_gpio freertos
)
