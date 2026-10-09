# privacy_guard

Owns guardian consent and local sensitive-data deletion for the device.
Audio upload, image upload, conversation history, and usage analytics all
start denied. A guardian must explicitly grant consent for each class, and a
revocation takes effect immediately.

The guard stores only consent booleans and a revision counter. It never stores
audio, images, or conversation content. `privacy_guard_clear_local_data` calls
the registered cache-clear callback on unbind, factory reset, or guardian
request; a callback failure is surfaced so the caller can retry or enter safe
mode instead of claiming deletion succeeded.

Host tests inject an in-memory storage interface. Production uses
`config_store` over NVS; a missing or incompatible record resets to deny-all.
