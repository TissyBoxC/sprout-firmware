# audio_input

Owns the conversation capture path for the device. It reads one 20 ms PCM frame
at a time from `audio_pipeline`, cleans it, detects speech, encodes Opus, and
hands the packet to the voice session.

## Pipeline

1. **Echo cancellation** subtracts the loudspeaker reference from the
   microphone using a normalized least-mean-squares adaptive filter. A
   Geigel-style double-talk detector freezes adaptation while the child speaks
   over the device so their voice is not learned as echo.
2. **Noise suppression** splits each frame into three perfect-reconstruction
   bands (rumble, vowel band, consonant band) and applies a smoothed
   Wiener-style gain per band. Band gains keep speech intelligible while a fan
   or air conditioner is pushed down.
3. **Voice activity detection** uses frame energy against an adaptive noise
   floor plus a zero-crossing window, with a minimum speech duration and a
   hangover so short pauses stay inside one utterance.
4. **Gain calibration** normalizes speech toward a target RMS with separate
   attack and release rates and a saturation-safe cap.
5. **Opus framing** encodes speech frames and emits them through the caller's
   callback, keeping a two-frame pre-roll so the first syllable is not clipped.

## Safety and privacy

- The microphone is disabled at boot and only enabled between `audio_input_start`
  and `audio_input_stop`.
- `audio_input_start` acquires the shared capture lease for the conversation;
  if wake detection still owns it, start fails instead of splitting the I2S
  stream.
- The reference sink is cleared on stop, and the capture task joins with a
  bounded timeout before resources are released.
- Frame buffers are fixed size; a stalled consumer cannot grow memory without
  bound.
- Raw audio is never logged.

## Public API

```c
esp_err_t audio_input_init(void);
esp_err_t audio_input_start(uint32_t stream_id,
                            audio_input_frame_callback_t callback,
                            void *context);
esp_err_t audio_input_stop(void);
esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out);
void audio_input_shutdown(void);
```

The callback runs on the capture task, so it must copy any packet it retains and
return quickly. It must not call any `audio_input` lifecycle function.

## Configuration

Every stage is tunable through `menuconfig` under `FEATURE_AUDIO_INPUT`: the
echo filter length, step size, leakage, and double-talk margin; the noise
floor tracking rates and retained gain; the gain target and bounds; and the
VAD threshold, minimum speech frames, and hangover.

## Removal

Disable `CONFIG_FEATURE_AUDIO_INPUT`, then remove the module registration from
`src/main.c` and the conditional dependency from `src/CMakeLists.txt`. The
`audio_pipeline` reference sink and the rest of the audio path keep working.
