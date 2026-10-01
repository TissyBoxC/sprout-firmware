# module_registry

Owns module registration, initialization order, and lifecycle state.

Each removable feature exports one `module_descriptor_t`. Removing a feature
means deleting its component, its Kconfig entry, its CMake condition, and one
conditional registration in the composition root.
