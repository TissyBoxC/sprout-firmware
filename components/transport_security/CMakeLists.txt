set(transport_security_sources "")

if(CONFIG_FEATURE_TRANSPORT_SECURITY)
    list(APPEND transport_security_sources
        "src/transport_security.c"
        "src/transport_security_core.c"
    )
endif()

idf_component_register(
    SRCS ${transport_security_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_common
    REQUIRES module_registry
)
