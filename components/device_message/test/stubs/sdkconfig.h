#pragma once

// The device_message host test compiles the moderation boundary in so the
// rejected-copy path is exercised the same way production runs.
#define CONFIG_FEATURE_CONTENT_FILTER 1
