#pragma once
#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(size_t,size_t);
void vQueueDelete(QueueHandle_t);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t);
BaseType_t xQueueSend(QueueHandle_t,const void *,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void *,TickType_t);
