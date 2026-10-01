set(error_code_sources "")

if(CONFIG_FEATURE_ERROR_CODE)
    list(APPEND error_code_sources "src/error_code.c")
endif()

idf_component_register(
    SRCS ${error_code_sources}
    INCLUDE_DIRS "include"
    REQUIRES module_registry
)
