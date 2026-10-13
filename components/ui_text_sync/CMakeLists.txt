set(ui_text_sync_sources "")

if(CONFIG_FEATURE_UI_TEXT_SYNC)
    list(APPEND ui_text_sync_sources
        "src/ui_text_sync.cpp"
        "src/ui_text_sync_core.c"
    )
endif()

idf_component_register(
    SRCS ${ui_text_sync_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        freertos
    REQUIRES
        config_store
        device_binding_client
        device_identity
        module_registry
        network_manager
        time_sync
        ui_text
)

# The requests carry the device session token, so the shared transport policy is
# referenced whenever the component is built and the policy is present. The
# guard also keeps the empty (feature-disabled) component from becoming an
# INTERFACE library, which cannot take a private link dependency.
if(CONFIG_FEATURE_UI_TEXT_SYNC AND CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
