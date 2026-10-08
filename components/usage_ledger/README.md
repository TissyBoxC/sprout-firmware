# usage_ledger

Persists the device-local usage that the platform bills and reports: active
seconds, conversation count and duration, content playback count and duration,
per-category totals, blocked-attempt counters, the last applied policy revision,
and the pending-upload queue. The platform receives the data through
`POST /api/v1/devices/{device_id}/runtime/usage`, and uploads are idempotent per
device and report date.

## What is stored per local day

`usage_ledger_day_t` holds one local calendar day keyed by
`time_sync_local_day_key`. It contains:

* `active_seconds` (saturating at one day),
* `conversation_count` and `conversation_seconds`,
* `content_play_count` and `content_seconds`,
* `categories[]` with per-category `play_count` and `seconds`,
* `blocked{}` counters for disabled period, daily limit, category denied, and
  time untrusted,
* `last_policy_version`, the guardian policy revision in force that day.

The head plus the pending queue are stored as one blob in encrypted NVS. The
blob carries `state_version`; a mismatch (upgrade or corruption) discards the
cached blob and starts an empty ledger rather than interpreting old bytes.

## Correctness guarantees

* **Cross-midnight rollover.** The first record of a new local day closes the
  previous day into the pending queue and starts a fresh one.
* **Monotonic-safe time.** Active seconds accumulate from the caller-supplied
  monotonic delta, never from wall-clock subtraction. A clock rollback cannot
  shrink an accumulated total.
* **Clock rollback.** If the local day key moves backwards, the newer day is
  kept and the caller is expected to surface `TIME_UNTRUSTED`; totals are never
  written into an earlier day.
* **Restart/power-loss durability.** Counter updates are flushed at most every
  `USAGE_LEDGER_FLUSH_INTERVAL_SECONDS`, on day rollover, and on
  `usage_ledger_flush()` (called on shutdown). At most one flush interval of
  active time is at risk, never the whole day.
* **Offline accumulation.** Completed days are held in a bounded queue
  (`USAGE_LEDGER_PENDING_CAPACITY`, oldest first). When the queue is full the
  oldest day is evicted and `evicted_day_count` is incremented and logged, so a
  very long outage loses the oldest usage first and makes the loss observable.
* **Idempotent upload.** `usage_ledger_mark_upload_confirmed(day_key)` removes a
  day only after the platform confirms it. A retry re-sends the same day and the
  platform replaces it by device + report date, so nothing double counts.

## Upload payload

`usage_ledger_get_pending_upload` builds one `usage_ledger_upload_payload_t`
matching `device_usage_upload.schema.json`:

```json
{
  "schema_version": "1.0.0",
  "report_date": "2026-10-07",
  "timezone_offset_minutes": 480,
  "active_seconds": 2520,
  "conversation_count": 18,
  "conversation_seconds": 1440,
  "content_play_count": 6,
  "content_seconds": 1080,
  "categories": [
    { "category": "story", "play_count": 3, "seconds": 540 }
  ],
  "blocked": {
    "disabled_period": 1,
    "daily_limit": 0,
    "category_denied": 2,
    "time_untrusted": 0
  }
}
```

The payload carries no conversation text, audio, images, tokens, or personal
data. `report_date` is derived from the day key with the same civil-from-days
math used for local time so it is stable across timezones.

## Typical call sequence

1. `usage_ledger_init()` on boot, after `config_store` is ready.
2. `usage_ledger_set_active(true/false, ...)` around an active session; the
   component folds the elapsed monotonic interval in on each transition.
3. `usage_ledger_record_conversation(...)`, `usage_ledger_record_content_playback(...)`,
   and `usage_ledger_record_blocked(...)` as events complete.
4. `usage_ledger_close_current_day()` on explicit shutdown and at midnight.
5. `usage_ledger_get_pending_upload()` then, after a 2xx response,
   `usage_ledger_mark_upload_confirmed(day_key)`.

## Configuration

`CONFIG_FEATURE_USAGE_LEDGER` gates the component and depends on
`FEATURE_CONFIG_STORE` and `FEATURE_TIME_SYNC`. The component is removable and
is deleted before `time_sync` and `config_store` in the verification script.
