# device_provisioning

Owns the first-run and re-provisioning flow. The module starts the official
Espressif BLE provisioning service, writes accepted Wi-Fi credentials through
`network_manager`, and returns the one-time `sprout://` binding URI through the
`custom-data` endpoint.

Manufacturing injects a unique SRP salt and verifier into NVS as the binary
keys `provisioning_srp_salt` and `provisioning_srp_verifier`. A device without
both values keeps provisioning disabled; the firmware never falls back to a
shared or empty proof of possession.

The binding URI is created only after the device receives valid Wi-Fi
credentials and authenticates with `device_platform`. The URI is not placed in
the BLE advertisement because it contains a short-lived, single-use token.
The guardian application reads it from the `custom-data` endpoint, asks the
platform to consume the token, and the device polls binding status before
stopping the BLE service and reconnecting the stored Wi-Fi credentials.

The module is removable. Removing it removes the BLE service, provisioning
dependencies, and binding URI exchange while leaving device registration,
certificate-backed authentication, and stored Wi-Fi reconnect behavior intact.
