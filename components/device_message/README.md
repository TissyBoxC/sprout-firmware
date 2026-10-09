# Device Message

Consumes the platform `display_message` device runtime command and presents one
bounded guardian-authored message.

## Contract

The platform runtime command carries an authored payload:

```json
{
  "notification_id": "…",
  "title": "记得喝水哦",
  "body": "爸爸妈妈希望你今天多喝水。",
  "severity": "info",
  "duration_seconds": 30,
  "category": "family_message"
}
```

`device_message_present` validates the copy against the
`notification_device_message` contract: title required, rune bounds enforced,
severity restricted to `info`/`success`/`warning`/`critical`, and duration
clamped to 1–300 seconds.

## Behavior

- A build with the `DISPLAY` capability renders the message for the given
  duration.
- A build without a display still records the message, confirms the command so
  the platform does not retry it forever, and fires a bounded LED cue.

The module never stores or renders child conversation content, audio, images,
credentials, or raw upstream data.
