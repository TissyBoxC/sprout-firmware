#pragma once

#include "freertos/FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, UBaseType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
