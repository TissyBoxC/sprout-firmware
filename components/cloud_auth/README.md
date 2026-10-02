# cloud_auth

Owns the device session lifecycle after `device_binding_client` has completed
its ECDSA challenge. It waits for a trusted clock, authenticates when needed,
refreshes before expiry, and clears the local session after an authorization
rejection. It never stores or logs the platform service token.

`CONFIG_FEATURE_CLOUD_AUTH` controls source inclusion. Delete the component,
its Kconfig entry, and its registration to remove it. The device can still
provision and bind, but authenticated runtime reporting remains unavailable.
