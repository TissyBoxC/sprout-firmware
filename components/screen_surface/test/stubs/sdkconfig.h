#pragma once

// The default screen-surface host test does not link the content filter, so
// the moderation boundary stays compiled out unless a fixture overrides this
// define on the command line.
#ifndef CONFIG_FEATURE_CONTENT_FILTER
#define CONFIG_FEATURE_CONTENT_FILTER 0
#endif
