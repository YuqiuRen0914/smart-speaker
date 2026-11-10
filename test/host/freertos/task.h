#pragma once
#include "FreeRTOS.h"
struct tskTaskControlBlock;
using TaskHandle_t=tskTaskControlBlock *;
void vTaskDelay(TickType_t);
