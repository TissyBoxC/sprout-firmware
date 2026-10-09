# content_filter

Owns child-safe text moderation for conversation input and output. It returns a
stable action and reason code for every turn:

- `allow`: the text is safe to send or play.
- `crisis_intervention`: the child may be describing self-harm or abuse. The
  text is not blocked; the response path must add a guardian-aware safe reply.
- `block`: the text matches a prohibited category such as sexual content,
  violence, gambling, drugs, prompt injection, or inducement to meet offline.

Output moderation checks self-harm instructions before generic rules, so an
instructional reply cannot be treated as a normal crisis response. The filter
is pure logic: it does not log, store, or transmit the text it evaluates.
