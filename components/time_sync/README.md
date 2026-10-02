# time_sync

Synchronizes the Sprout device clock over SNTP after Wi-Fi connects. The module
keeps a small persisted trusted-time marker so a reboot can distinguish a
plausible clock from a factory-reset clock before the first successful sync.
The platform heartbeat can also supply an authoritative time so a blocked SNTP
server does not prevent TLS or scheduled work.

`CONFIG_FEATURE_TIME_SYNC` controls source inclusion. Delete the component, its
Kconfig entry, and its registration in `main.c` to remove it. When removed,
`cloud_auth` rejects platform sessions until the system clock is valid.
