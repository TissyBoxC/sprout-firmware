#pragma once

#include "FreeRTOS.h"

QueueHandle_t xQueueCreate(
    unsigned long length,
    unsigned long item_size
);
void vQueueDelete(QueueHandle_t queue);
int xQueueSend(
    QueueHandle_t queue,
    const void *item,
    TickType_t timeout
);
int xQueueReceive(
    QueueHandle_t queue,
    void *item,
    TickType_t timeout
);
