#pragma once

// error_recovery only calls esp_restart from the unrecoverable path, which the
// host test deliberately does not exercise. The declaration keeps the
// translation unit compiling.
void esp_restart(void);
