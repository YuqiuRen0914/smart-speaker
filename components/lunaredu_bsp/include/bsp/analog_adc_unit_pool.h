#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>

namespace lunaredu::device {

esp_err_t acquire_shared_adc_oneshot_unit(
    adc_unit_t unit, adc_oneshot_unit_handle_t *handle);
void release_shared_adc_oneshot_unit(adc_unit_t unit,
                                     adc_oneshot_unit_handle_t handle);

} // namespace lunaredu::device