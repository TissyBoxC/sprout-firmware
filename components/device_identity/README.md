# device_identity

Owns the stable identifier used when a device registers with the platform.
The identifier is derived from the factory MAC but the raw MAC is never placed
in a device payload. It also owns the device's unique ECDSA P-256 signing key:
the private key is generated once, kept in the configured NVS partition, and
never exported after provisioning.

The private key is stored as a 32-byte raw scalar in NVS because the platform
challenge signature must be available after reboot. Production builds must
enable NVS encryption and Flash Encryption for the identity partition.

`CONFIG_FEATURE_DEVICE_IDENTITY` controls source inclusion and the composition
root dependency. Delete the component, its Kconfig entry, its CMake condition,
and the composition-root registration to remove it from a build.
