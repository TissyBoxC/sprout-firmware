# parent_policy

Fetches, validates, persists, and serves the effective parent policy for the
authenticated device. The module consumes:

```text
GET /api/v1/devices/{device_id}/runtime/parent-policy
Authorization: Bearer <device_session_token>
```

The platform must return the standard success envelope with one `data`
object containing `policy_version`, `daily_limit_minutes`,
`allowed_categories`, `disabled_periods`, `max_volume_percent`, and
`updated_at`, plus `schema_version`, `source_child_count`, and
`aggregation_mode: "most_restrictive"`. `policy_version` is the 64-bit family
revision and must never decrease; equal versions are accepted as an idempotent
read, while lower versions are rejected as stale. A 404 with the
`parent_policy_not_found` error code clears the cache; a route-level 404 or
any other failure leaves the last valid policy intact and records the reason
in the module status. A 401 clears the platform session through `cloud_auth`.

The cached values are stored in `config_store` under the short `pp_` key
prefix. `max_volume_percent` is applied immediately through
`volume_control_set_max_percent`; clearing the policy restores the compiled
volume ceiling. The module depends on `config_store`, `device_binding_client`,
`device_identity`, and `volume_control`.

`CONFIG_FEATURE_PARENT_POLICY` controls source inclusion. The module can be
removed with `tools/verify_removable_modules.ps1`, which deletes the dependent
`device_runtime_reporter` first and then removes this component, its
composition-root registration, and its application dependency.

Background refreshes are rate-limited by
`CONFIG_PARENT_POLICY_REFRESH_INTERVAL_SECONDS`. Explicit
`refresh_configuration` commands always bypass that interval.
