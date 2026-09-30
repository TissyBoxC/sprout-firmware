# ui_text

Owns the optional device-side remote UI text module.

`CONFIG_FEATURE_UI_TEXT` controls both source inclusion and the application
dependency. When disabled, the component is registered with no sources and is
not linked.

For firmware, only text keys covered by the generated font subset and the
declared maximum length may be activated. A package that exceeds Flash,
memory, font, language, or compatibility limits is rejected and the device
continues using built-in text.
