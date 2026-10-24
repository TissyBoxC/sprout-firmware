set(device_runtime_reporter_sources "")

if(CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER)
    list(APPEND device_runtime_reporter_sources "src/device_runtime_reporter.cpp")
    if(CONFIG_FEATURE_DEVICE_MQTT AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../device_mqtt/CMakeLists.txt")
        list(APPEND device_runtime_reporter_sources
            "src/device_runtime_mqtt_service.c"
        )
    endif()
endif()

set(device_runtime_reporter_requires
    cloud_auth
    config_store
    device_binding_client
    module_registry
    network_manager
    network_quality
    offline_fallback
    time_sync
)

set(device_runtime_reporter_priv_requires
    esp_app_format
    esp_http_client
    esp-tls
    freertos
    mbedtls
)

if(CONFIG_FEATURE_DEVICE_IDENTITY)
    list(APPEND device_runtime_reporter_requires device_identity)
endif()

# Keep the optional integration buildable when parent_policy is deleted. The
# source guard removes the calls at the same time.
if(CONFIG_FEATURE_PARENT_POLICY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../parent_policy/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "parent_policy")
endif()

# The reporter routes session commands to these two modules, so the component
# link must follow the same feature flags the source guard uses.
if(CONFIG_FEATURE_CARE_VIEW AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../care_view/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "care_view")
endif()

if(CONFIG_FEATURE_VOICE_INTERCOM AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../voice_intercom/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "voice_intercom")
endif()

if(CONFIG_FEATURE_ASSESSMENT_SYNC AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../assessment_sync/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "assessment_sync")
endif()

if(CONFIG_FEATURE_USAGE_LEDGER AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../usage_ledger/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "usage_ledger")
endif()

# Thermal telemetry is optional. When the component is absent the heartbeat
# simply omits the temperature block.
if(CONFIG_FEATURE_TEMPERATURE_MONITOR AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../temperature_monitor/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "temperature_monitor")
endif()

if(CONFIG_FEATURE_PARENT_CONTROL_RUNTIME AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../parent_control_runtime/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "parent_control_runtime")
endif()

# Provisioning state is optional in minimal images; the heartbeat falls back
# to the persisted binding state when the component is absent.
if(CONFIG_FEATURE_DEVICE_PROVISIONING AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../device_provisioning/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "device_provisioning")
endif()

# Diagnostics are optional and remain compatible with the original heartbeat
# contract when this component is removed.
if(CONFIG_FEATURE_DIAGNOSTIC_REPORTER AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../diagnostic_reporter/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "diagnostic_reporter")
endif()

# Provisioning audit events are optional and remain compatible with an image
# that does not contain the provisioning reporter.
if(CONFIG_FEATURE_PROVISIONING_REPORTER AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../provisioning_reporter/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "provisioning_reporter")
endif()

if(CONFIG_FEATURE_FACTORY_RESET AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../factory_reset/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "factory_reset")
endif()

# Guardian device messages are optional. When the component is absent the
# reporter acknowledges display_message with a stable result instead of
# failing the whole runtime.
if(CONFIG_FEATURE_DEVICE_MESSAGE AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../device_message/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "device_message")
endif()

# The guardian consent-sync command and the usage-analytics gate both rely on
# privacy_guard. When the component is absent the reporter drops its consent
# command and skips the analytics gate rather than failing to link.
if(CONFIG_FEATURE_PRIVACY_GUARD AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../privacy_guard/CMakeLists.txt")
    list(APPEND device_runtime_reporter_requires "privacy_guard")
endif()

# The MQTT command bridge is optional and private to this component. When the
# contract component is absent the runtime keeps the authenticated HTTP
# polling and acknowledgement path.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../device_mqtt/CMakeLists.txt")
    list(APPEND device_runtime_reporter_priv_requires device_mqtt)
endif()

idf_component_register(
    SRCS ${device_runtime_reporter_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${device_runtime_reporter_priv_requires}
    REQUIRES ${device_runtime_reporter_requires}
)
