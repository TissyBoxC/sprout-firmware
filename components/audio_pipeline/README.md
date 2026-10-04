# audio_pipeline

Owns the I2S controller that carries the microphone input and the speaker
output. It converts between the fixed 20 ms PCM frame and the I2S DMA, and it
does not encode or decode; that belongs to `audio_codec`.

The microphone and speaker share one I2S controller so they use the same bit
clock and word select. Capture and playback are enabled separately, and both
start disabled. A device therefore never records until the wake detector or a
guardian enables capture, which satisfies the no-covert-capture rule.

Capture reads exactly one full frame per call and reports a partial read as an
error instead of forwarding misaligned audio. Playback blocks at most
`AUDIO_PIPELINE_PLAYBACK_TIMEOUT_MS`, which bounds back pressure when the
network stalls.

Capture has a single logical owner. Wake detection and conversation capture
must call `audio_pipeline_capture_acquire` before reading and pass that same
owner to `audio_pipeline_capture_frame`. The acquire and release operations are
atomic, so a second owner is rejected with `ESP_ERR_INVALID_STATE` instead of
interleaving bytes from the same I2S RX stream. The owner check and I2S read
share one mutex, and releasing a lease is the only capture path that disables
the channel while an owner is active.

`audio_pipeline_set_capturing` remains available for low-level code that does
not need a logical owner. It cannot disable capture while an owner holds a
lease; the owner must release it through
`audio_pipeline_capture_release`.

Pins, DMA geometry, and timeouts come from Kconfig, so a board with a different
microphone or amplifier is a configuration change and not a code change. The
defaults match the ESP32-S3-N16R8 breadboard wiring and must be reviewed before
a production board is declared.

Delete the component, its Kconfig entry, and its registration to remove audio
hardware support; `audio_codec` and the rest of the system still build.
