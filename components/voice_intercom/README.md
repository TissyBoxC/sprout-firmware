# voice_intercom

Guardian-initiated voice intercom for 如此萌屋 · 芽系列 · 初芽.

A guardian starts a call from the parent application, the device rings with a
visual indication and an audible prompt, and the child must physically press
the button before any audio is captured or transmitted. Either side can end the
call, and the device always releases the audio path immediately.

## Contract

### Switch

`CONFIG_FEATURE_VOICE_INTERCOM` (default `n`). The module only exists in the
build when the switch is on; `src/CMakeLists.txt` and `src/main.c` gate every
reference on it.

| Kconfig option | Default | Meaning |
| --- | --- | --- |
| `CONFIG_VOICE_INTERCOM_RING_TIMEOUT_SECONDS` | 30 (10–120) | How long the device rings before it declines on its own. |
| `CONFIG_VOICE_INTERCOM_MAX_CALL_SECONDS` | 300 (30–1800) | Hard upper bound for one call, enforced on the device. |

Both bounds are enforced by the module itself through `voice_intercom_poll`.
The platform may also end a call, but it can never extend it.

### Phases

`IDLE → RINGING → CONNECTING → ACTIVE → IDLE`

| Phase | Microphone | Realtime session | Indication |
| --- | --- | --- | --- |
| `IDLE` | off | none | `LED_INDICATOR_STATE_IDLE` |
| `RINGING` | off | none | `LED_INDICATOR_STATE_LISTENING`, ring card, prompt tone |
| `CONNECTING` | no | started | `LED_INDICATOR_STATE_CONNECTING` |
| `ACTIVE` | leased | running | `LED_INDICATOR_STATE_SPEAKING` |

Ringing captures nothing and transmits nothing. The realtime session and the
microphone lease are only taken inside `voice_intercom_accept`, which is
reachable from the button handler and the public accept entry point only.

### Signalling

The module owns its own mutual-TLS MQTT client, opened only while a call phase
needs it, against the same broker configuration as the rest of the device
(`mqtt_url`, `mqtt_ca`, `mqtt_cert`, `mqtt_key`). Only `mqtts://` is accepted;
a cleartext or unprovisioned broker refuses the accept instead of downgrading.

Topics:

- subscribe `sprout/v1/devices/{device_id}/commands`
- publish `sprout/v1/devices/{device_id}/acks`

Commands inside a `device.command` envelope:

| `command_type` | `data` | Effect |
| --- | --- | --- |
| `voice_intercom_call` | `call_id`, `caller_name` | Starts the ring. Refused while a call is already in progress. |
| `voice_intercom_call_control` | `call_id`, `action` = `hangup` \| `decline` | Ends the matching call. Any other action, including `accept`, is ignored. |

The guardian cannot accept a call: only the child's physical press opens the
microphone. Acknowledgements publish `status` and a stable `result_code`
(`accepted`, `local_decline`, `remote_hangup`, `ring_timeout`, `max_duration`,
`connect_failed`, `transport_closed`) and a hashed command identifier.

### Button mapping

| Gesture | Ringing | Active |
| --- | --- | --- |
| Short press | Accept | Hang up |
| Long press | Decline | Hang up |

Other gestures are ignored so the existing device gestures keep their meaning.

### Privacy

- No capture, and no traffic, before the physical accept.
- Audio upload passes `privacy_guard_evaluate(PRIVACY_GUARD_DATA_AUDIO_UPLOAD)`.
- Call identifiers, tokens and any audio content never reach the log sink. Only
  the module tag and stable reason names are logged.
- The screen shows the guardian-supplied display name and the accept
  instruction; nothing about the call content.
- Signalling credentials are cleared as soon as the client closes.

## Tests

Host tests compile the module logic without ESP-IDF:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_host_tests.ps1
```

- `test/test_voice_intercom_core.cpp` covers the pure state machine: no audio
  flag outside an accepted call, ring timeout, connect stall, the duration
  bound anchored at the moment the call becomes active, overlapping calls,
  identifier validation, countdown rounding and clock wrap.
- `test/test_voice_intercom_runtime.cpp` covers the runtime against stub
  dependencies: ringing captures and sends nothing, accept starts audio and one
  acknowledgement, every end path releases the session, capture lease, output
  and indication, the on-device timeout, guardian control that cannot accept,
  cleartext or unprovisioned brokers, offline devices and untrusted payloads.

Firmware builds:

```powershell
pio run -e esp32-s3-n16r8
```

## Remove safely

1. Set `# CONFIG_FEATURE_VOICE_INTERCOM is not set` (the default).
2. Delete `components/voice_intercom`.
3. Remove the guarded block from `src/main.c` (module registration, button
   routing and the periodic `voice_intercom_poll` call).
4. Remove the guarded `app_requires` entry from `src/CMakeLists.txt`.
5. Drop the two `voice_intercom` entries from `tools/run_host_tests.ps1` and the
   `voice_intercom` entry from `$removableModules` in
   `tools/verify_removable_modules.ps1`.

No other component depends on this module, so nothing else needs to change.
