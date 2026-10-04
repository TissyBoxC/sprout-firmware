# audio_output

Owns the final speaker render path. It mixes concurrent 16 kHz mono PCM frame
lanes, applies the effective volume, and writes exactly one whole frame to
`audio_pipeline`.

## Mixing model

Each priority has its own bounded queue:

- `ambient` for background content
- `conversation` for spoken replies
- `prompt` for acknowledgements and reminders
- `safety` for announcements that must bypass mute

The mixer pops at most one frame from each non-empty lane for one output
period. It scales every lane before summing and saturates the sum at the
16-bit bounds, so concurrent audio cannot wrap into noise. This is what allows
a prompt or safety tone to play over a conversation instead of replacing it.

## Volume and safety

When `volume_control` is enabled, normal lanes use its effective percentage
and therefore respect mute. The safety lane uses the guardian maximum, raised
to `CONFIG_AUDIO_OUTPUT_SAFETY_MIN_PERCENT` when needed, so a safety
announcement remains audible even when the device is muted. Without
`volume_control`, the compiled fallback volume keeps the module functional.

## Flush and echo reference

`audio_output_discard_priority` removes queued frames from one lane under the
same mutex used by the mixer, which makes preemption and clear operations
thread-safe. `audio_output_flush` provides a wider teardown operation that can
optionally include safety frames.

The mixer calls `audio_pipeline_play_frame` only after the final mixed frame is
complete. The existing `audio_pipeline` reference sink therefore sees the
actual waveform sent to I2S, which is the correct echo-cancellation reference.

## Removal

Disable `CONFIG_FEATURE_AUDIO_OUTPUT`, remove its registration from
`src/main.c`, and remove the conditional dependency from `src/CMakeLists.txt`.
`playback_queue` and `prompt_tone` preserve their direct pipeline fallback for
a build that intentionally removes the mixer.
