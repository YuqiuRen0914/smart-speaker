#include "bsp/analog_adc_unit_pool.h"

#include <mutex>
#include <stdint.h>

#include <soc/soc_caps.h>

namespace lunaredu::device {

namespace {

std::mutex g_mutex;
adc_oneshot_unit_handle_t g_handles[SOC_ADC_PERIPH_NUM] = {};
uint16_t g_ref_counts[SOC_ADC_PERIPH_NUM] = {};

int adc_unit_index(adc_unit_t unit) {
  switch (unit) {
  case ADC_UNIT_1:
    return 0;
#if SOC_ADC_PERIPH_NUM > 1
  case ADC_UNIT_2:
    return 1;
#endif
  default:
    return -1;
  }
}

} // namespace

esp_err_t acquire_shared_adc_oneshot_unit(
    adc_unit_t unit, adc_oneshot_unit_handle_t *handle) {
  if (handle == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  *handle = nullptr;

  const int index = adc_unit_index(unit);
  if (index < 0 || index >= SOC_ADC_PERIPH_NUM) {
    return ESP_ERR_INVALID_ARG;
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_handles[index] == nullptr) {
    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = unit;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    const esp_err_t result = adc_oneshot_new_unit(&unit_config, &g_handles[index]);
    if (result != ESP_OK) {
      return result;
    }
  }

  if (g_ref_counts[index] == UINT16_MAX) {
    return ESP_ERR_NO_MEM;
  }
  g_ref_counts[index] += 1;
  *handle = g_handles[index];
  return ESP_OK;
}

void release_shared_adc_oneshot_unit(adc_unit_t unit,
                                     adc_oneshot_unit_handle_t handle) {
  if (handle == nullptr) {
    return;
  }

  const int index = adc_unit_index(unit);
  if (index < 0 || index >= SOC_ADC_PERIPH_NUM) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_handles[index] != handle || g_ref_counts[index] == 0) {
    return;
  }

  g_ref_counts[index] -= 1;
  if (g_ref_counts[index] == 0) {
    if (adc_oneshot_del_unit(g_handles[index]) == ESP_OK) {
      g_handles[index] = nullptr;
    }
  }
}

esp_err_t release_shared_adc_oneshot_unit_checked(
    adc_unit_t unit, adc_oneshot_unit_handle_t handle) {
  if (handle == nullptr) return ESP_OK;
  const int index = adc_unit_index(unit);
  if (index < 0 || index >= SOC_ADC_PERIPH_NUM) return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_handles[index] != handle || g_ref_counts[index] == 0)
    return ESP_ERR_INVALID_STATE;
  if (g_ref_counts[index] == 1) {
    const esp_err_t result = adc_oneshot_del_unit(handle);
    if (result != ESP_OK) return result;
    g_handles[index] = nullptr;
  }
  --g_ref_counts[index];
  return ESP_OK;
}

} // namespace lunaredu::device
