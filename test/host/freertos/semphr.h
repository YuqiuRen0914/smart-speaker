#pragma once
using SemaphoreHandle_t=void *;
SemaphoreHandle_t xSemaphoreCreateBinary();
void vSemaphoreDelete(SemaphoreHandle_t);
