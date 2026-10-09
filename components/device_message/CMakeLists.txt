set(device_message_sources "")

if(CONFIG_FEATURE_DEVICE_MESSAGE)
    list(APPEND device_message_sources "src/device_message.c")
endif()

# The pure core carries the shared validation rules and is always compiled so
# host tests can exercise it without the ESP-IDF runtime.
list(APPEND device_message_sources "src/device_message_core.c")

set(device_message_requires
    device_capabilities
    module_registry
)

if(CONFIG_FEATURE_LED_INDICATOR AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../led_indicator/CMakeLists.txt")
    list(APPEND device_message_requires "led_indicator")
endif()

idf_component_register(
    SRCS ${device_message_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${device_message_requires}
)
