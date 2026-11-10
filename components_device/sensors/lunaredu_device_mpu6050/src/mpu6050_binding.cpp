#include "lunaredu_device_mpu6050_imu.h"

#include <string>

#include "bsp/lunaredu_bsp.h"
#include <esp_log.h>

namespace lunaredu::device {

bool setup_main_imu_module() {
  const bsp_i2c_bus_t *bus = bsp_mpu_i2c_bus();
  const esp_err_t i2c_result = bsp_i2c_init(bus);
  if (i2c_result != ESP_OK) {
    const bsp_resource_claim_snapshot_t claim =
        bsp_resource_last_claim_snapshot();
    ESP_LOGE("mpu6050", "mpu i2c init failed: %s owner=%s pin=%s",
         esp_err_to_name(i2c_result),
         claim.conflict_owner[0] != '\0' ? claim.conflict_owner : "(none)",
         claim.conflict_pin[0] != '\0' ? claim.conflict_pin : "(none)");
    return false;
  }

  Mpu6050ModuleConfig config;
  config.sda_pin = bus->sda_pin;
  config.scl_pin = bus->scl_pin;
  config.i2c_clock_hz = bus->clock_hz;
  config.preferred_address = 0x68;
  config.allow_secondary_address_fallback = true;
  config.has_interrupt_pin = false;
  config.default_sample_rate_hz = 50;

  if (!setup_mpu6050(config)) {
    ESP_LOGW("mpu6050", "main imu setup skipped or failed");
    return false;
  }

  return true;
}

} // namespace lunaredu::device
