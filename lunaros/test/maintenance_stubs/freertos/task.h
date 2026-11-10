#pragma once
#include "FreeRTOS.h"
void vTaskDelay(TickType_t);
TaskHandle_t xTaskGetCurrentTaskHandle();
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, TickType_t);
