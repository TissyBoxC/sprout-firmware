#pragma once

#include "esp_err.h"

/* Keep the module TAG referenced so strict host builds do not flag it. */
#define ESP_LOGI(tag, format, ...) ((void)(tag))
#define ESP_LOGW(tag, format, ...) ((void)(tag))
#define ESP_LOGE(tag, format, ...) ((void)(tag))
