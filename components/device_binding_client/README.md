# device_binding_client

Owns the device side of registration, challenge authentication, and binding
confirmation with `device_platform`:

1. Register the device public key with a manufacturing registration grant.
2. Request a challenge nonce and sign `device_id + "." + nonce` with the
   ECDSA P-256 key from `device_identity`.
3. Request a single-use binding token while holding the device session token.
4. Poll binding status so the device leaves the provisioning screen once a
   guardian binds it.

All transport is HTTPS with the ESP certificate bundle; there is no insecure
fallback. The platform base URL is read from `config_store` under
`platform_base_url` and must start with `https://`.

`CONFIG_FEATURE_DEVICE_BINDING_CLIENT` controls source inclusion. Delete the
component, its Kconfig entry, and the `main.c` registration to remove it.
