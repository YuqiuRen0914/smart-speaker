#pragma once

#include <stdint.h>
#include <esp_err.h>

// Board wiring and electrical timing only. Pixel formats, frame buffers and
// capture lifetime are owned by the Camera Device component.
struct bsp_camera_board_config_t {
  int pin_pwdn = -1;
  int pin_reset = -1;
  int pin_xclk = -1;
  int pin_sccb_sda = -1;
  int pin_sccb_scl = -1;
  int pin_d0 = -1;
  int pin_d1 = -1;
  int pin_d2 = -1;
  int pin_d3 = -1;
  int pin_d4 = -1;
  int pin_d5 = -1;
  int pin_d6 = -1;
  int pin_d7 = -1;
  int pin_vsync = -1;
  int pin_href = -1;
  int pin_pclk = -1;
  uint32_t xclk_freq_hz = 0;
};

// Always available, including boards without a camera. Does not claim hardware
// or initialize a driver. Unsupported boards return a cleared descriptor.
esp_err_t bsp_camera_get_board_config(bsp_camera_board_config_t *config);
