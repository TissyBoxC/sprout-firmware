# network_manager

Owns the Wi-Fi station lifecycle: driver start, stored-credential reconnect,
IP acquisition, and the connection state exposed to other modules.

The device never connects before provisioning writes credentials, and it
reconnects automatically after a disconnect using the stored values. Passwords
are held in `config_store` and are never logged.

`CONFIG_FEATURE_NETWORK_MANAGER` controls source inclusion and the
composition-root dependency. Delete the component, its Kconfig entry, and the
`main.c` registration to remove it.
