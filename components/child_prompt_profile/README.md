# child_prompt_profile

`child_prompt_profile` holds the bounded child-facing prompt context: age tier,
allowed content categories, the effective volume ceiling and whether a
guardian policy is present.

## Contract

- `child_prompt_profile_init` loads the persisted age tier and the cached
  guardian policy. It is idempotent and never widens a missing policy.
- An unknown tier resolves to the most conservative bucket so absent data
  cannot expose content meant for an older child.
- `child_prompt_profile_snapshot` returns a copy; callers never observe a
  half-updated profile while a policy sync is running.

## Remove safely

Delete the component, its `Kconfig` entry and its row in
`components/module_registry` registration in `src/main.c`, then rebuild the
profiles that do not include prompt personalisation.
