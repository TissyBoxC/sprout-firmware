# playback_queue

Owns outbound audio scheduling for the single speaker. Replies, prompts,
reminders, and tones compete for the same output, so the queue applies an
explicit priority order and mirrors the gateway playback queue.

Priorities run from `PLAYBACK_PRIORITY_AMBIENT` to
`PLAYBACK_PRIORITY_SAFETY`. A higher priority item preempts a running item only
when that item is interruptible; the unplayed tail is copied into PSRAM and
played after the interrupting item. `PLAYBACK_PRIORITY_SAFETY` is never
interruptible and is never dropped by mute, so a crisis or guardian prompt
always reaches the child.

Items and their frames are allocated in PSRAM because one item holds up to 32
frames of 640 bytes. The worker blocks on a binary semaphore, so an idle queue
consumes no CPU, and it re-reads the stop reason under the mutex before every
frame so a preemption or clear takes effect within one 20 ms frame.

When `volume_control` is enabled the worker scales each frame immediately
before `audio_pipeline_play_frame`. The stored frame stays unscaled, so an item
resumed after an interruption is scaled exactly once by the volume active at
that moment.

Delete the component, its Kconfig entry, and its registration to remove
prioritized playback; callers then fall back to direct playback.
