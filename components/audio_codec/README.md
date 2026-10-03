# audio_codec

Owns the single realtime audio profile used by the microphone capture path and
the speaker playback path: 16 kHz, mono, 20 ms Opus frames.

The module wraps the Espressif `esp_audio_codec` encoder and decoder so neither
the capture code nor the playback queue has to know how Opus is initialised.
Encoding and decoding are serialised by one mutex because the capture task and
the playback task can run concurrently on different cores.

An empty packet is decoded as Opus packet-loss concealment. That keeps the
playback timeline aligned when a network frame is dropped instead of inserting
silence. `audio_codec_reset` clears prediction state between conversations
because Opus carries state across packets.

`CONFIG_FEATURE_AUDIO_CODEC` controls source inclusion and the
`esp_audio_codec` dependency. Delete the component, its Kconfig entry, and its
registration to remove audio entirely; capability flags then stop advertising
`audio_input` and `audio_output`.
