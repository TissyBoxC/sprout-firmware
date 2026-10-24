# voice_session

`voice_session` owns the device side of a voice conversation: the state
machine, binary audio envelopes, capture and playback leases, interruption,
timeouts and bounded conversation context.

## Contract

- Audio envelopes carry a magic, version 1, a 24-byte header, sequence,
  timestamp, session id and at most `VOICE_SESSION_MAX_PAYLOAD_BYTES` (1024) of
  Opus payload in a uint16 length field.
- Device audio uses the SRAW magic and gateway audio uses SRSV; an unexpected
  magic, version or length is rejected without starting playback.
- Device video uses the SRVD magic with version 2 and a 26-byte header so the
  payload length is a uint32 and a JPEG still can reach
  `VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES` (256 KiB). The magic selects the
  version, header size and cap, so a still carrying version 1 or an audio frame
  carrying version 2 is refused. The audio output is byte-for-byte unchanged.
- `voice_session_send_video_frame` gates stills on IMAGE-upload consent rather
  than audio consent and drops the frame when consent is missing. It builds the
  envelope on the heap, because a still is far too large for a task stack, and
  frees it on every path. `video_frames_sent` and `video_frames_dropped` report
  the outcome through the snapshot.
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
and the real sender poll path with controlled host dependencies. The protocol
tests cover the SRVD wire bytes, the v2 header and uint32 length, the per-magic
version and cap, the unchanged audio layout, and the video rejections.

The host test does not validate ESP-SR acoustic detection, microphone capture,
TLS/WebSocket delivery, or real wake-word accuracy. Those require the target
board and production model partition. `voice_session_send_video_frame` has no
host test: it needs the runtime stubs, so its test belongs in
`test/run_voice_session_wake_runtime.ps1`, which is outside the component's
current wiring.

## Remove safely

Delete the component and its Kconfig switch, then remove voice session
registration in `src/main.c`. Voice features must be disabled in that profile.
