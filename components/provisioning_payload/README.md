# provisioning_payload

Builds and parses the `sprout://device?...` payload shown as a QR code by
display-equipped devices. The payload carries only the single-use binding
token, the platform device identifier, and the display name.

No credential, private key, or guardian identity is placed in the code. The
display name is percent-encoded so the Chinese product name remains a valid
URI component.

`CONFIG_FEATURE_PROVISIONING_PAYLOAD` controls source inclusion. The QR module
is intentionally separate from the network stack so a device without a screen
can omit this component entirely.
