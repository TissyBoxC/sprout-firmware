#pragma once

#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;

QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size);
BaseType_t xQueueSend(
    QueueHandle_t queue,
    const void *item,
    TickType_t ticks
);
BaseType_t xQueueReceive(
    QueueHandle_t queue,
    void *item_out,
    TickType_t ticks
);
void vQueueDelete(QueueHandle_t queue);
