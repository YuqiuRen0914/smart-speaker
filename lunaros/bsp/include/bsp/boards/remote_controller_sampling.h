#pragma once
#include "bsp/boards/remote_controller.h"
#include "bsp/lunaredu_bsp.h"
namespace lunaredu::bsp::remote_controller {
// PCB-specific configuration derivation. The caller supplies driver config
// types and task definitions; BSP neither registers owners nor starts workers.
template<class Config> Config imu_sampling_config(Config config) {
  config.sensor.sda_pin=kImuSdaPin;config.sensor.scl_pin=kImuSclPin;
  config.sensor.i2c_clock_hz=100000;config.sensor.use_bsp_i2c=false;
  config.sensor.default_sample_rate_hz=50;config.sensor.enable_pose=true;
  config.sensor.preferred_address=kImuPreferredI2cAddress;
  config.diagnostic_scan=true;return config;
}
template<class Config,class Panel> Config touch_sampling_config(Config config,const Panel &panel) {
  config.bus={"i2c.touch",kTouchSdaPin,kTouchSclPin,100000,BSP_RESOURCE_REMOTE_CONTROLLER_TOUCH};
  config.bus_index=1;config.interrupt_pin=kTouchInterruptPin;
  config.width=panel.width;config.height=panel.height;
  config.transform={panel.swap_xy,!panel.mirror_x,!panel.mirror_y};return config;
}
}
