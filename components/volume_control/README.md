# volume_control

Owns the single speaker volume value and the guardian maximum in percent.
Every write is clamped to the maximum so a loud request, a stale client, or a
replayed command cannot exceed the limit the family configured.

The policy maximum and the current volume are stored through `config_store`
when that module is present. A profile that removes `config_store` still
enforces the compiled default and the cap; it only loses persistence across
reboots.

`volume_control_apply_gain` scales a decoded 16-bit PCM frame in place. Scaling
saturates instead of wrapping, so a full-scale passage stays audible instead of
turning into noise, and mute zeroes the frame. The playback path calls this
immediately before `audio_pipeline_play_frame`, which keeps the policy inside
the audio path instead of relying on the caller to remember it.

Delete the component, its Kconfig entry, and its registration to remove volume
control; the rest of the audio path keeps working at fixed full scale.
