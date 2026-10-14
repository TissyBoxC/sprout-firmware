#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t diagnostic_reporter_record_interaction(
    const char *event_type,
    const char *detail_code,
    uint32_t duration_ms
);
