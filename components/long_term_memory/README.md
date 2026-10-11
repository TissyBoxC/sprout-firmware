# Long-Term Memory

Stores a bounded set of guardian-authorized facts about a child so the
companion can recall preferences, routines and milestones across sessions. It
completes the `长记忆` part of the platform's companionship feature.

## Contract

- Nothing is stored without an explicit guardian authorization. With
  authorization off, `long_term_memory_remember` is rejected, `recall` returns
  false, and `list`/`count` report zero.
- Revoking authorization (`long_term_memory_set_authorized(false)`) erases every
  stored entry so an unauthorized memory can never be read back.
- Memory is keyed by child id plus a stable key, so re-learning one fact
  replaces the old value instead of growing duplicates.
- The store holds at most `LONG_TERM_MEMORY_CAPACITY` (16) entries; values are
  bounded to `LONG_TERM_MEMORY_VALUE_SIZE` bytes.
- `long_term_memory_forget` deletes one entry and
  `long_term_memory_forget_child` deletes every entry for a child, giving the
  guardian a complete deletion path.

## Storage

The authorization flag and the bounded store persist as one binary blob in
`config_store`. A missing or corrupt blob starts from an empty, unauthorized
store so a corrupt store never fabricates consent or blocks startup. The module
never stores audio, images, conversation transcripts, credentials, or raw
upstream data.
