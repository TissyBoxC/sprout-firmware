# Keep the project application component explicit because PlatformIO uses
# `src` as its source directory rather than the ESP-IDF default `main`.
set(app_requires "")

if(CONFIG_FEATURE_ERROR_CODE)
    list(APPEND app_requires "error_code")
endif()

if(CONFIG_FEATURE_DEVICE_IDENTITY)
    list(APPEND app_requires "device_identity")
endif()

if(CONFIG_FEATURE_DEVICE_CAPABILITIES)
    list(APPEND app_requires "device_capabilities")
endif()

if(CONFIG_FEATURE_DEVICE_PROVISIONING)
    list(APPEND app_requires "device_provisioning")
endif()

if(CONFIG_FEATURE_ERROR_RECOVERY)
    list(APPEND app_requires "error_recovery")
endif()

if(CONFIG_FEATURE_MODULE_REGISTRY)
    list(APPEND app_requires "module_registry")
endif()

if(CONFIG_FEATURE_SYSTEM_CORE)
    list(APPEND app_requires "system_core")
endif()

if(CONFIG_FEATURE_UI_TEXT)
    list(APPEND app_requires "ui_text")
endif()

if(CONFIG_FEATURE_VERSION_INFO)
    list(APPEND app_requires "version_info")
endif()

idf_component_register(
    SRCS "main.c"
    REQUIRES ${app_requires}
)
