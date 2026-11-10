#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>

namespace lunaredu::device {

esp_err_t acquire_shared_adc_oneshot_unit(
    adc_unit_t unit, adc_oneshot_unit_handle_t *handle);
void release_shared_adc_oneshot_unit(adc_unit_t unit,
                                     adc_oneshot_unit_handle_t handle);
// On failure the caller still owns its reference and must retain the handle.
// A successful release consumes exactly one reference, including shared units.
esp_err_t release_shared_adc_oneshot_unit_checked(
    adc_unit_t unit, adc_oneshot_unit_handle_t handle);

} // namespace lunaredu::device
