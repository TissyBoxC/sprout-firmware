# voice_wake

Owns local wake detection for the device. It consumes one 16 kHz mono 20 ms
PCM frame at a time from `audio_pipeline`, feeds the selected detector, and
reports accepted wake events through a bounded callback.

## Backends

Two backends exist so production behavior and deterministic verification stay
separate but use one lifecycle and event contract:

- `CONFIG_VOICE_WAKE_BACKEND_ESP_SR` is the production backend. It initializes
  ESP-SR's AFE, loads the selected WakeNet model from the `model` partition,
  and feeds AFE-sized PCM chunks.
- `CONFIG_VOICE_WAKE_BACKEND_DETERMINISTIC` is a real energy and
  zero-crossing detector for CI, host verification, and removable-module
  builds. It requires a sustained speech-like run and applies the same
  confidence, cooldown, playback-ignore, suspend, and callback rules as the
  production backend.

ESP-SR's fetch result does not expose a probability score. The production
backend therefore reports the configured WakeNet threshold as the confidence
floor, which keeps the public contract truthful without inventing a value.

## Lifecycle and safety

- `audio_pipeline` starts with the microphone disabled.
- `voice_wake_start` and `voice_wake_resume` are the only operations that
  enable capture.
- `voice_wake_stop` disables capture and leaves the backend disarmed.
- `voice_wake_suspend` disables capture while preserving the armed flag;
  `voice_wake_resume` enables it again only when armed.
- The detector is suppressed during speaker playback and for
  `VOICE_WAKE_PLAYBACK_IGNORE_MS` after playback ends.
- A threshold check, cooldown, and one bounded capture task prevent runaway
  callbacks and unbounded work.
- Raw audio is never logged or retained.

## Public API

```c
esp_err_t voice_wake_init(void);
bool voice_wake_is_ready(void);
esp_err_t voice_wake_start(void);
esp_err_t voice_wake_stop(void);
esp_err_t voice_wake_suspend(void);
esp_err_t voice_wake_resume(void);
esp_err_t voice_wake_set_wake_handler(voice_wake_handler_t handler,
                                      void *context);
esp_err_t voice_wake_get_snapshot(voice_wake_snapshot_t *snapshot_out);
const char *voice_wake_error_name(voice_wake_error_t error);
const module_descriptor_t *voice_wake_module_descriptor(void);
```

The event contains a wake word id and name, confidence, timestamp, and a
session nonce. The callback runs on the wake task and must return promptly.

## ESP-SR model and partition

For the production backend, add a custom partition named `model` and select
the custom partition table. A 4 MB model partition leaves the existing 4 MB
factory application unchanged on the 16 MB N16R8 layout:

```csv
# Name,   Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        4M,
model,    data, spiffs,  ,        4M,
```

Required `sdkconfig.defaults` entries for the production backend are:

```text
CONFIG_FEATURE_VOICE_WAKE=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_MODEL_IN_FLASH=y
CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y
```

The wake phrase is a product decision. `ni hao xiao zhi` is used here as a
low-risk default because it is one of the bundled Chinese wake models; change
the `CONFIG_SR_WN_*` symbol and the model-name filter together if the product
chooses another phrase. Keep `CONFIG_SR_NSN_WEBRTC=y` and
`CONFIG_SR_VADN_WEBRTC=y` to avoid unnecessary model-partition growth.

## Removal

Disable `CONFIG_FEATURE_VOICE_WAKE`, remove its registration from `src/main.c`,
and remove the conditional dependency from `src/CMakeLists.txt`. The rest of
the audio path keeps working, and the deterministic backend can be selected
for a build without ESP-SR model data.
