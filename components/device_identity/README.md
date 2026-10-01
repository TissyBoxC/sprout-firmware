# device_identity

Owns the stable identifier used when a device registers with the platform.
The identifier is derived from the factory MAC but the raw MAC is never placed
in a device payload.

`CONFIG_FEATURE_DEVICE_IDENTITY` controls source inclusion and the composition
root dependency. Delete the component, its Kconfig entry, its CMake condition,
and the composition-root registration to remove it from a build.
