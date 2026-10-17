# conversation_context

`conversation_context` keeps a small bounded window of recent turn summaries so
the device can label a voice session and build a short offline fallback prompt.
The gateway remains the authoritative multi-turn history owner.

## Contract

- At most `CONVERSATION_CONTEXT_CAPACITY` (8) turns are retained; the oldest is
  evicted first.
- Each turn records role, a bounded summary, sequence number and creation time.
- Initialisation clears retained turns so a new session cannot leak a previous
  child conversation.

## Tests

`conversation_context_ring` and `conversation_context_runtime` run under
`tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch, then remove the context calls in
`voice_session`.
