set(alarm_reminder_sources "")

if(CONFIG_FEATURE_ALARM_REMINDER)
    list(APPEND alarm_reminder_sources
        "src/alarm_reminder.c"
        "src/alarm_reminder_core.c"
    )
endif()

idf_component_register(
    SRCS ${alarm_reminder_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        module_registry
)
