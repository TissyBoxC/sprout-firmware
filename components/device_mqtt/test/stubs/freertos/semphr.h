#pragma once

#include "FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
