set(device_mqtt_sources "")

if(CONFIG_FEATURE_DEVICE_MQTT)
    list(APPEND device_mqtt_sources
        "src/device_mqtt.c"
        "src/device_mqtt_core.c"
        "src/device_mqtt_transport.c"
    )
endif()

idf_component_register(
    SRCS ${device_mqtt_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__mqtt freertos
    REQUIRES module_registry
)
