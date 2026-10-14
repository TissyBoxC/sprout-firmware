# Device MQTT

Owns the firmware-side half of the shared device MQTT contract before any
broker connection is opened. The component is deliberately transport-free:
topics, payloads, deduplication, and acknowledgements are unit-tested on the
host, while TLS and the production broker endpoint stay in the composition
root.

## Contract

- Commands arrive on `sprout/v1/devices/{device_id}/commands` at QoS 1 and are
  not retained.
- The device id comes from the topic, never from the JSON payload.
- A command envelope must use `schema_version` `1.0.0`, `event_type`
  `device.command`, and `subject.type` `device`. The subject id and payload
  routing fields must agree with the topic device id.
- `command_id`, `command_type`, and `request_id` are required and bounded.
  The envelope `request_id` must match `payload.request_id`.
- The deduplication ledger keeps the most recent `DEVICE_MQTT_DEDUPE_CAPACITY`
  (16) command ids so a QoS 1 redelivery cannot execute an operator command
  twice. The ledger resets on reboot.
- Acknowledgements are `device.command_ack` envelopes published at QoS 1 on
  `sprout/v1/devices/{device_id}/commands/ack`; the HTTP acknowledgement route
  remains a compatible fallback.

## Boundary

This component does not open a socket, read a certificate, or embed a broker
hostname. The composition root may only connect after the MQTT domain,
certificate chain, and client provisioning are confirmed for production.
