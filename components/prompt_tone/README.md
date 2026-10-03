# prompt_tone

Renders the device's built-in cues without downloading audio: wake accepted,
capture started and stopped, microphone muted, volume limit reached, network
lost and restored, battery low, and the safety announcement.

Every cue is a short sequence of tone segments built from a 64-entry sine
table. Phase advances in Q16, so any frequency is produced with integer
arithmetic and one table lookup per sample instead of a per-sample `sin` call.
Cues stay within `PROMPT_TONE_MAX_FRAMES` whole 20 ms frames, so the render
buffer is bounded and lives once in the module rather than on a caller stack.

When `playback_queue` is enabled the cue is queued at its priority, so the
safety announcement is queued at `PLAYBACK_PRIORITY_SAFETY`, cannot be
preempted, and is never silenced by mute. Without the queue the cue is written
straight to the speaker, which keeps acknowledgment and error feedback working
in a minimal build.

The parent application maps each identifier from `prompt_tone_name` to
explanatory text. The device therefore emits a stable identifier instead of
embedding wording that would need an OTA to correct.

Delete the component, its Kconfig entry, and its registration to remove system
cues; the rest of the audio path keeps working.
