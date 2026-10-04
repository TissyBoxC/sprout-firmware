# factory_reset

Owns the guarded local factory reset state machine. A request only arms the
module; it never erases by itself. The caller must confirm the request within
`CONFIG_FACTORY_RESET_PENDING_TIMEOUT_SECONDS`, otherwise the pending request
expires and confirmation is rejected.

`factory_reset_request` records the guardian request, button gesture,
provisioning reset, or internal recovery reason. `factory_reset_confirm` is the
only path that erases data. `factory_reset_cancel` clears the armed request.
A one-shot `esp_timer` owns the timeout, and every state read checks expiry as
well, so no caller can confirm an expired request.

When `diagnostic_reporter` is present, the module emits the bounded
`factory_reset_requested`, `factory_reset_cancelled`, `factory_reset_completed`,
and `factory_reset_failed` interaction events at the real state transition.
Timeouts are reported as cancellations with the original request reason.
Events use the stable reason or error name as `detail_code`, `duration_ms` is
zero, and no free text, credential, or child data is stored.

The erase is deliberately narrow: it removes the public configuration keys
through `config_store` when that removable module is present, then erases the
single NVS partition named by `CONFIG_FACTORY_RESET_NVS_PARTITION` with
`nvs_flash_erase_partition`. It never erases the bootloader, application,
partition table, or any other data partition. The same partition is
reinitialized after the erase so subsequent provisioning storage starts clean.

The snapshot exposes `is_pending`, `request_reason`, `requested_at_ms`,
`last_result`, `completed_count`, and `is_ready`. Public names and errors are
stable ASCII identifiers for diagnostics and the parent application.

Delete the component, its `CONFIG_FEATURE_FACTORY_RESET` Kconfig entry, and its
composition-root registration to remove local reset handling. The optional
`config_store` and `button_input` dependencies are private and guarded, so
their removal does not require changes inside this module.

When `button_input` is present, the application can map its very-long press
gesture to `factory_reset_request(FACTORY_RESET_REASON_BUTTON_GESTURE)`.
`factory_reset` does not claim the shared button handler, so short and double
press gestures remain available to the rest of the product.
