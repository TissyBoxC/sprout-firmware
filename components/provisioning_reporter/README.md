# provisioning_reporter

Owns the bounded, non-sensitive provisioning and connectivity audit state sent
with the periodic `device_runtime` heartbeat. It records the twelve stable
contract event types, assigns monotonic event identities, and retains events
until a successful authenticated heartbeat acknowledges them.

The state uses one short NVS key and a fixed-size event array of sixteen
records. Each record contains only an event identity, contract event type,
sequence, symbolic detail code, duration, and firmware version. SSIDs,
passwords, tokens, audio, images, and child data are never stored or sent.

`CONFIG_FEATURE_PROVISIONING_REPORTER` controls source inclusion. Removing the
component removes the heartbeat extension while leaving provisioning,
authentication, and the base runtime contract usable.
