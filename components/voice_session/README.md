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

## Tests

`voice_session_protocol`, `voice_session_token` and `voice_duplex_core` run
under `tools/run_host_tests.ps1`. They cover the envelope contract, token
verification and duplex interruption behavior.

## Remove safely

Delete the component and its Kconfig switch, then remove voice session
registration in `src/main.c`. Voice features must be disabled in that profile.
