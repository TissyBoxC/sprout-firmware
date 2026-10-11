set(habit_reminder_sources "")

if(CONFIG_FEATURE_HABIT_REMINDER)
    list(APPEND habit_reminder_sources
        "src/habit_reminder.c"
        "src/habit_reminder_core.c"
    )
endif()

idf_component_register(
    SRCS ${habit_reminder_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        config_store
    REQUIRES
        module_registry
)
