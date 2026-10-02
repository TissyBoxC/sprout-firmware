# config_store

Persists small runtime settings in NVS for other feature modules. It owns the
Wi-Fi credential keys, the provisioning state key, and the platform base URL
key so no other module needs to know NVS layout or namespaces.

The module never logs, echoes, or returns stored passwords. Production images
must enable NVS encryption; without it, credentials are readable from flash.

`CONFIG_FEATURE_CONFIG_STORE` controls source inclusion. Delete the component,
its Kconfig entry, and the composition-root registration to remove it.
