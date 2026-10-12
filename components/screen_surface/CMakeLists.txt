set(screen_surface_sources "")

if(CONFIG_FEATURE_SCREEN_SURFACE)
    list(APPEND screen_surface_sources
        "src/screen_surface.c"
        "src/screen_core.c"
    )
endif()

idf_component_register(
    SRCS ${screen_surface_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        module_registry
        config_store
)

# Moderation is optional. When the filter is removed the point-to-read path
# keeps its existing behavior and no moderation symbol is referenced. A
# minimal profile compiles every source out, which makes COMPONENT_LIB an
# INTERFACE target that has no link step, so the block is also gated on the
# component having sources.
if(CONFIG_FEATURE_SCREEN_SURFACE)
    if(CONFIG_FEATURE_CONTENT_FILTER AND EXISTS
            "${CMAKE_CURRENT_LIST_DIR}/../content_filter/CMakeLists.txt")
        idf_component_optional_requires(PRIVATE content_filter)
    endif()
endif()
