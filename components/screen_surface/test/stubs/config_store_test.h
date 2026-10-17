#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void screen_config_store_test_reset(void);
void screen_config_store_test_set_set_result(esp_err_t result);

#ifdef __cplusplus
}
#endif
