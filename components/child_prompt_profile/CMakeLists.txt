set(child_prompt_profile_sources "")

if(CONFIG_FEATURE_CHILD_PROMPT_PROFILE)
    list(APPEND child_prompt_profile_sources "src/child_prompt_profile.c")
endif()

idf_component_register(
    SRCS ${child_prompt_profile_sources}
    INCLUDE_DIRS "include"
    REQUIRES config_store module_registry
)

# The parent policy adapter is optional: the profile keeps working with its
# persisted tier when parent_policy is removed from the image. The optional
# link is only added when this component has a real library, because a
# disabled component is registered as an INTERFACE library.
if(CONFIG_FEATURE_CHILD_PROMPT_PROFILE AND CONFIG_FEATURE_PARENT_POLICY)
    idf_component_optional_requires(PRIVATE parent_policy)
endif()
