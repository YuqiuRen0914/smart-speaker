#pragma once
#include "FreeRTOS.h"
void vTaskDelay(TickType_t);
void xTaskNotifyGiveIndexed(TaskHandle_t,UBaseType_t);
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t,BaseType_t,TickType_t);
