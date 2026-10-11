# ui_text

Owns the optional device-side remote UI text module. It stores one active
signed package in NVS and resolves text keys against it before falling back to
built-in text, so a device renders the last known good copy when the network or
the platform is unavailable.

`CONFIG_FEATURE_UI_TEXT` controls both source inclusion and the application
dependency. When disabled, the component is registered with no sources and is
not linked.

For firmware, only text keys covered by the generated font subset and the
declared maximum length may be activated. A package that exceeds Flash,
memory, font, language, or compatibility limits is rejected and the device
continues using built-in text.

`ui_text_apply_package` refuses to roll text back: a package version older than
the active one is ignored, so an out of order delivery cannot regress the user
interface. A withdrawal clears the active package and reverts to built-in text.
The pure merge logic lives in `src/ui_text_store.c` and is covered by the host
test `ui_text_store` in `firmware/tools/run_host_tests.ps1`.
