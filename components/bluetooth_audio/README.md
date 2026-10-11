# bluetooth_audio

`bluetooth_audio` owns the bounded Bluetooth audio contract, the validated
persist/restore path and the pure policy and adapter helpers the audio loop
consumes. It never touches content or voice state, so a firmware profile
without a Bluetooth radio simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `preferred_codec` auto..ldac
- `max_volume_percent` 0..100
- `display_name` up to 48 bytes (plus terminator)
- `enabled`, `speaker_mode_allowed`, `pairing_allowed`, `auto_reconnect`
  booleans

The default policy keeps Bluetooth audio and pairing off, so a device never
advertises or accepts a pairing without an explicit guardian decision.

## Behaviour

`bluetooth_audio_start_pairing` refuses advertising unless the policy is enabled
and pairing is authorised, so the device never exposes a pairing surface a
guardian did not approve. `bluetooth_audio_connect` is refused while disabled and
resolves a bounded active codec only once connected. `bluetooth_audio_disconnect`
clears speaker mode and the active codec. `bluetooth_audio_set_speaker_mode`
requires a live connection and an authorised policy, and leaving the connected
state always clears it. `bluetooth_audio_set_enabled(false)` drives the adapter
off and revokes speaker mode and pairing in one step, and
`bluetooth_audio_report_volume` clamps every report to the guardian ceiling.

## Host tests

`bluetooth_core` and `bluetooth_audio_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, the default-off policy, the
pairing/connect/disconnect transitions, the speaker-mode gate, the policy
revoke on disable, the volume ceiling and the persistence round trip.
