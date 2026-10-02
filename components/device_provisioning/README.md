# device_provisioning

Owns the first-run and re-provisioning flow. The module starts the official
Espressif BLE provisioning service, writes accepted Wi-Fi credentials through
`network_manager`, and returns the one-time `sprout://` binding URI through the
`custom-data` endpoint.

On first boot, or when the locally stored credential set is incomplete, the
module generates a per-device proof of possession and an SRP verifier. The
verifier is persisted as `provisioning_srp_verifier`, the proof is persisted as
`provisioning_pop`, and a fixed 16-byte salt is persisted as
`provisioning_srp_salt`. The advertised setup URI contains the service name and
proof so a display-equipped device can show it directly; it never contains a
shared secret or a platform token.

The guardian application uses the setup URI to scan for the BLE service, send
the selected Wi-Fi credentials through the official Security 2 session, and
then read the one-time binding payload from `custom-data`. That payload is
created only after the device joins Wi-Fi, consumes its persisted manufacturing
registration grant when needed, and authenticates with `device_platform`. The
device keeps polling binding status until the guardian consumes the token, then
stops the BLE service and reconnects the stored Wi-Fi credentials.

The module is removable. Removing it removes the BLE service, provisioning
dependencies, and binding URI exchange while leaving device registration,
certificate-backed authentication, and stored Wi-Fi reconnect behavior intact.
