# transport_security

Owns the device-wide transport security policy. Every outbound HTTPS, WSS, and
MQTTS endpoint is validated through this module before a client is created.

Peer and hostname verification are always enabled. Cleartext `http`, `ws`, and
`mqtt` URLs are rejected, embedded credentials are rejected, and an endpoint
cannot be replaced by a lower-security protocol. The module performs pure URL
validation only; it never disables certificate validation or opens a socket.

`transport_security_validate_url` returns a parsed host, port, path, and
protocol. Callers still attach the ESP-IDF certificate bundle to their client
so a failed TLS handshake is fatal rather than silently downgraded.
