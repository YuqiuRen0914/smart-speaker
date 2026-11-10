#include "legacy/expansion_baseboard/pins.h"
#include "bsp/lunaredu_camera.h"
#include "sdkconfig.h"

esp_err_t bsp_camera_get_board_config(bsp_camera_board_config_t *config) {
  if (config == nullptr) return ESP_ERR_INVALID_ARG;
  *config = {};
#if CONFIG_LUNAREDU_BSP_HAS_ONBOARD_CAMERA && CONFIG_LUNAROS_BSP_ENABLE_LEGACY_BASEBOARD
  config->pin_pwdn = lunaredu::bsp::pins::kCameraPowerDown;
  config->pin_reset = lunaredu::bsp::pins::kCameraReset;
  config->pin_xclk = lunaredu::bsp::pins::kCameraXclk;
  config->pin_sccb_sda = lunaredu::bsp::pins::kCameraSiod;
  config->pin_sccb_scl = lunaredu::bsp::pins::kCameraSioc;
  config->pin_d0 = lunaredu::bsp::pins::kCameraY2;
  config->pin_d1 = lunaredu::bsp::pins::kCameraY3;
  config->pin_d2 = lunaredu::bsp::pins::kCameraY4;
  config->pin_d3 = lunaredu::bsp::pins::kCameraY5;
  config->pin_d4 = lunaredu::bsp::pins::kCameraY6;
  config->pin_d5 = lunaredu::bsp::pins::kCameraY7;
  config->pin_d6 = lunaredu::bsp::pins::kCameraY8;
  config->pin_d7 = lunaredu::bsp::pins::kCameraY9;
  config->pin_vsync = lunaredu::bsp::pins::kCameraVsync;
  config->pin_href = lunaredu::bsp::pins::kCameraHref;
  config->pin_pclk = lunaredu::bsp::pins::kCameraPclk;
  config->xclk_freq_hz = 20000000;
  return ESP_OK;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}
