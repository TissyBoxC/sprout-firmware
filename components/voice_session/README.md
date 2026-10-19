# voice_session

`voice_session` owns the device side of a voice conversation: the state
machine, binary audio envelopes, capture and playback leases, interruption,
timeouts and bounded conversation context.

## Contract

- Envelopes carry a magic, version, sequence, timestamp, session id and at
  most `VOICE_SESSION_MAX_PAYLOAD_BYTES` (1024) of Opus payload.
- Device audio uses the SRAW magic and gateway audio uses SRSV; an unexpected
  magic, version or length is rejected without starting playback.
- The capture lease is released on every terminal path so a failed session
  cannot hold the microphone.
- The state mirror follows idle, listening, thinking and speaking, and a
  timeout or interruption returns the session to idle.
- The sender observes `voice_wake` detections through the snapshot event
  sequence without replacing the feedback handler. The first valid event
  starts a session; duplicate snapshots are ignored; a wrapped sequence is
  handled as one observation; and the bounded wake name is sent rather than a
  hard-coded phrase.
- A failed session start clears the wake transaction state. A new detection
  that arrives while the previous handshake is still opening is coalesced into
  that session and delivered after `session_started`.

## Tests

`voice_session_protocol`, `voice_session_token`, `voice_duplex_core`,
`voice_session_wake_observer` and `voice_session_wake_runtime` run under
`tools/run_host_tests.ps1`. The wake tests cover first observation, duplicate
snapshots, missed observations, `uint32_t` wrap, invalid names, start failure,
and the real sender poll path with controlled host dependencies.

The host test does not validate ESP-SR acoustic detection, microphone capture,
TLS/WebSocket delivery, or real wake-word accuracy. Those require the target
board and production model partition.

## Remove safely

Delete the component and its Kconfig switch, then remove voice session
registration in `src/main.c`. Voice features must be disabled in that profile.
