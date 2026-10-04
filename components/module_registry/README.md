# module_registry

Owns module registration, initialization order, and lifecycle state.

Each removable feature exports one `module_descriptor_t`. Removing a feature
means deleting its component, its Kconfig entry, its CMake condition, and one
conditional registration in the composition root.

Initialization runs in registration order, then retries modules that failed
after all other modules had one chance to start. `module_registry_initialize_module`
allows a supervisor to retry one failed module explicitly. The registry reports
failure and recovery transitions to `error_recovery`; no module needs to know
which diagnostics consumer is active.
