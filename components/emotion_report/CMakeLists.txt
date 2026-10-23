# The observation payload is built by the core with a bounded snprintf, so this
# component needs no JSON library: the request carries four scalar fields and
# nothing else, and a hand-built body cannot accidentally grow a field.
set(emotion_report_sources "")

if(CONFIG_FEATURE_EMOTION_REPORT)
    list(APPEND emotion_report_sources
        "src/emotion_report.cpp"
        "src/emotion_report_core.c"
    )
endif()

idf_component_register(
    SRCS ${emotion_report_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        companionship
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        device_binding_client
        device_identity
        module_registry
        network_manager
        time_sync
)

# The sample is sent with the device session token, so the shared transport
# policy is referenced whenever present and drops out otherwise. The dependency
# is only added when this component owns sources: with the feature disabled the
# component registers as an ESP-IDF INTERFACE (config-only) target, and
# idf_component_optional_requires would try
# target_link_libraries(... PRIVATE ...) on it, which CMake rejects.
if(CONFIG_FEATURE_EMOTION_REPORT
        AND CONFIG_FEATURE_TRANSPORT_SECURITY
        AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
