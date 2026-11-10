#pragma once
#include <cstddef>
#include "FreeRTOS.h"
using QueueHandle_t=void *;
QueueHandle_t xQueueCreate(size_t,size_t);
int xQueueSend(QueueHandle_t,const void *,TickType_t);
void vQueueDelete(QueueHandle_t);
