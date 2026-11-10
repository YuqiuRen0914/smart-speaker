#include "bsp/lunaredu_bsp.h"

#include <driver/gpio.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {


bsp_shared_pin_mode_t g_mode = BSP_SHARED_PIN_MODE_NONE;
constexpr const char *kOwner = "shared_pins";

SemaphoreHandle_t ensure_mutex() {
  static StaticSemaphore_t storage;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
  return mutex;
}

esp_err_t reset_shared_gpio_group() {
  // 这里先集中保留共享 GPIO 复位入口，后续接入可插拔 PWM/I2S/I2C 资源时，
  // 各模块不需要各自猜测和重置引脚状态。
  return ESP_OK;
}

esp_err_t acquire_mode_resource(bsp_shared_pin_mode_t mode) {
  switch (mode) {
  case BSP_SHARED_PIN_MODE_PWM3:
    return bsp_resource_acquire(BSP_RESOURCE_PWM_MOTOR_BANK, kOwner);
  case BSP_SHARED_PIN_MODE_I2S:
    return bsp_resource_acquire(BSP_RESOURCE_I2S_DATA, kOwner);
  case BSP_SHARED_PIN_MODE_BASEBOARD_MPU6050_I2C:
    return bsp_resource_acquire(BSP_RESOURCE_BASEBOARD_MPU6050_I2C, kOwner);
  case BSP_SHARED_PIN_MODE_NONE:
  default:
    return ESP_OK;
  }
}

esp_err_t release_mode_resource(bsp_shared_pin_mode_t mode) {
  switch (mode) {
  case BSP_SHARED_PIN_MODE_PWM3:
    return bsp_resource_release(BSP_RESOURCE_PWM_MOTOR_BANK, kOwner);
  case BSP_SHARED_PIN_MODE_I2S:
    return bsp_resource_release(BSP_RESOURCE_I2S_DATA, kOwner);
  case BSP_SHARED_PIN_MODE_BASEBOARD_MPU6050_I2C:
    return bsp_resource_release(BSP_RESOURCE_BASEBOARD_MPU6050_I2C, kOwner);
  case BSP_SHARED_PIN_MODE_NONE:
  default:
    return ESP_OK;
  }
}

} // 匿名命名空间

esp_err_t bsp_shared_pins_enter(bsp_shared_pin_mode_t mode) {
  SemaphoreHandle_t mutex = ensure_mutex();
  if (mutex == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (g_mode != BSP_SHARED_PIN_MODE_NONE) {
    xSemaphoreGive(mutex);
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t result = acquire_mode_resource(mode);
  if (result != ESP_OK) {
    xSemaphoreGive(mutex);
    return result;
  }

  result = reset_shared_gpio_group();
  if (result == ESP_OK) {
    g_mode = mode;
  } else {
    release_mode_resource(mode);
  }
  xSemaphoreGive(mutex);
  return result;
}

esp_err_t bsp_shared_pins_leave(bsp_shared_pin_mode_t mode) {
  SemaphoreHandle_t mutex = ensure_mutex();
  if (mutex == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (g_mode == mode) {
    esp_err_t result = reset_shared_gpio_group();
    if (result == ESP_OK) result = release_mode_resource(mode);
    if (result != ESP_OK) {
      xSemaphoreGive(mutex);
      return result;
    }
    g_mode = BSP_SHARED_PIN_MODE_NONE;
  }

  xSemaphoreGive(mutex);
  return ESP_OK;
}

bsp_shared_pin_mode_t bsp_shared_pins_get_mode() {
  SemaphoreHandle_t mutex = ensure_mutex();
  if (mutex == nullptr) {
    return BSP_SHARED_PIN_MODE_NONE;
  }

  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return BSP_SHARED_PIN_MODE_NONE;
  }

  const bsp_shared_pin_mode_t mode = g_mode;
  xSemaphoreGive(mutex);
  return mode;
}
