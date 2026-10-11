# Local Command

Caches a bounded set of guardian-published trigger phrases that map to local
device actions, so common commands keep working with no network. The platform
publishes the whole set as one revision; the device resolves a phrase locally
and never queues audio, images, credentials, or child data.

Each macro keeps a stable command id, a bounded trigger phrase, a stable action
(`play_package`, `pause`, `resume`, `stop`, `set_volume`, `sleep`), a bounded
argument, a revision, and an enabled flag.

## Contract

- The cache holds at most `LOCAL_COMMAND_CAPACITY` (24) commands.
- `local_command_replace_all` validates the entire batch before it touches the
  working set, so a malformed revision never partially replaces a good one.
- A target-bearing action (`play_package`, `set_volume`) must carry an argument;
  a stateless action (`pause`, `resume`, `stop`, `sleep`) must not, so a
  malformed macro cannot silently run against the wrong target.
- `local_command_resolve` trims surrounding whitespace, folds to lower case,
  and skips disabled commands, matching only enabled commands.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`local_command_replace_all` replaces and persists the whole set.
`local_command_list` copies the stored commands in publish order and
`local_command_resolve` maps one trigger phrase to its action.
