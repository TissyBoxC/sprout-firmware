set(parent_policy_sources "")

if(CONFIG_FEATURE_PARENT_POLICY)
    list(APPEND parent_policy_sources "src/parent_policy.cpp")
endif()

idf_component_register(
    SRCS ${parent_policy_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client esp-tls mbedtls
    REQUIRES cloud_auth config_store device_binding_client device_identity esp_timer module_registry volume_control
)

# Policy fetches carry the device session token, so the shared transport policy
# is referenced whenever it is present and drops out of minimal builds.
if(CONFIG_FEATURE_TRANSPORT_SECURITY AND EXISTS
        "${CMAKE_CURRENT_LIST_DIR}/../transport_security/CMakeLists.txt")
    idf_component_optional_requires(PRIVATE transport_security)
endif()
