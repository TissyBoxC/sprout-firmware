# Companionship

Provides a local, bounded encouragement reply keyed to a child's emotion, so
the device stays kind and responsive when the AI reply is unavailable. It
pairs with the `growth_record` and `habit_reminder` caches to complete the
offline side of the platform's companionship feature
(`/api/v1/companionship/...`).

## Contract

- Emotions are a small stable set: `neutral`, `happy`, `sad`, `angry`,
  `scared`, `tired`.
- `companionship_encourage` writes at most `COMPANIONSHIP_REPLY_SIZE` bytes
  and always NUL-terminates, so a long child name is truncated rather than
  overrunning the caller's buffer.
- The module has no persisted state and no network dependency; it is a pure
  fallback. It never claims content the platform did not provide and never
  stores conversation content or credentials.

## Behavior

The reply path is pure and deterministic, so the AI path can prefer the model
reply when it is available and fall back to `companionship_encourage` when it
is not.
