#pragma once

#include "freertos/FreeRTOS.h"

/* Declared, not inlined: the host stand-ins live in audio_codec_deps.c so the
   test can force mutex creation and take failures. */
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, UBaseType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
