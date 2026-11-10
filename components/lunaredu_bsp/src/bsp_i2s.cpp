#include "bsp/lunaredu_bsp.h"
#include <cstdio>
#include <mutex>

namespace {
std::recursive_mutex g_i2s_mutex;
uint64_t g_next_lease = 0;
void owner_name(const bsp_i2s_resource_claim_t *claim, char *name, size_t size) {
  std::snprintf(name, size, "i2s:%llu", static_cast<unsigned long long>(claim->lease_id));
}
struct ResourceFlag { bsp_resource_t resource; bool bsp_i2s_resource_claim_t::*flag; };
constexpr ResourceFlag kFlags[] = {
  {BSP_RESOURCE_EXPANSION_SPI1, &bsp_i2s_resource_claim_t::expansion_spi1_claimed},
  {BSP_RESOURCE_EXPANSION_I2C1, &bsp_i2s_resource_claim_t::expansion_i2c1_claimed},
  {BSP_RESOURCE_BASEBOARD_MPU6050_I2C, &bsp_i2s_resource_claim_t::baseboard_i2c_claimed},
  {BSP_RESOURCE_PWM_MOTOR_BANK, &bsp_i2s_resource_claim_t::pwm_bank_claimed},
};
}

esp_err_t bsp_i2s_claim_pins(const int *pins, size_t pin_count,
                            bsp_i2s_resource_claim_t *claim) {
  if (claim == nullptr || pins == nullptr || pin_count == 0) return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::recursive_mutex> lock(g_i2s_mutex);
  if (claim->lease_id != 0) return ESP_ERR_INVALID_STATE;
  if (g_next_lease == UINT64_MAX) return ESP_ERR_NO_MEM;
  for (size_t i = 0; i < pin_count; ++i) {
    if (pins[i] < -1 || pins[i] >= GPIO_NUM_MAX) return ESP_ERR_INVALID_ARG;
  }
  *claim = {};
  claim->lease_id = ++g_next_lease;
  char owner[BSP_RESOURCE_CLAIM_OWNER_MAX];
  owner_name(claim, owner, sizeof(owner));
  esp_err_t result = ESP_OK;
  if (bsp_i2s_uses_shared_pins(pins, pin_count)) {
    result = bsp_shared_pins_enter(BSP_SHARED_PIN_MODE_I2S);
    claim->shared_i2s_entered = result == ESP_OK;
  }
  for (const auto &entry : kFlags) {
    if (result != ESP_OK) break;
    for (size_t i = 0; i < pin_count; ++i) {
      if (bsp_resource_contains_pin(entry.resource, pins[i])) {
        result = bsp_resource_acquire(entry.resource, owner);
        claim->*(entry.flag) = result == ESP_OK;
        break;
      }
    }
  }
  if (result != ESP_OK) {
    // Keep any failed rollback in the caller's claim so release can retry it.
    const esp_err_t cleanup = bsp_i2s_release_pins(claim);
    return cleanup == ESP_OK ? result : cleanup;
  }
  return ESP_OK;
}

esp_err_t bsp_i2s_release_pins(bsp_i2s_resource_claim_t *claim) {
  if (claim == nullptr) return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::recursive_mutex> lock(g_i2s_mutex);
  if (claim->lease_id == 0) return ESP_OK;
  char owner[BSP_RESOURCE_CLAIM_OWNER_MAX];
  owner_name(claim, owner, sizeof(owner));
  esp_err_t first_error = ESP_OK;
  for (const auto &entry : kFlags) {
    if (!(claim->*(entry.flag))) continue;
    const esp_err_t result = bsp_resource_release(entry.resource, owner);
    if (result == ESP_OK) claim->*(entry.flag) = false;
    else if (first_error == ESP_OK) first_error = result;
  }
  if (claim->shared_i2s_entered) {
    const esp_err_t result = bsp_shared_pins_leave(BSP_SHARED_PIN_MODE_I2S);
    if (result == ESP_OK) claim->shared_i2s_entered = false;
    else if (first_error == ESP_OK) first_error = result;
  }
  if (first_error == ESP_OK) *claim = {};
  return first_error;
}

bool bsp_i2s_uses_shared_pins(const int *pins, size_t pin_count) {
  if (pins == nullptr) {
    return false;
  }
  for (size_t index = 0; index < pin_count; ++index) {
    if (bsp_resource_contains_pin(BSP_RESOURCE_I2S_DATA, pins[index])) {
      return true;
    }
  }
  return false;
}
