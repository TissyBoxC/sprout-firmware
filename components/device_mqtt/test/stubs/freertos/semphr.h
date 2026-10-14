#pragma once

#include "FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer);
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
