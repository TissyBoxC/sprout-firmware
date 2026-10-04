set(led_indicator_sources "")

if(CONFIG_FEATURE_LED_INDICATOR)
    list(APPEND led_indicator_sources "src/led_indicator.c")
endif()

idf_component_register(
    SRCS ${led_indicator_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
    PRIV_REQUIRES freertos esp_driver_ledc
)
