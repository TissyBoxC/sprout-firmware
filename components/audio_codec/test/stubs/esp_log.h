#pragma once

#include "esp_err.h"

/* The log macros reference their tag so -Werror=unused-const-variable stays
   quiet for the module TAG in host builds. */
#define ESP_LOGI(tag, format, ...) ((void)(tag))
#define ESP_LOGW(tag, format, ...) ((void)(tag))
#define ESP_LOGE(tag, format, ...) ((void)(tag))
