#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
using i2c_master_bus_handle_t=void*; using i2c_master_dev_handle_t=void*;
using gpio_num_t=int;
constexpr int GPIO_NUM_NC=-1, I2C_ADDR_BIT_LEN_7=0;
struct i2c_device_config_t {int dev_addr_length=0; unsigned device_address=0,scl_speed_hz=0;};
inline bool fail_probe_delete=false; inline int live_probes=0;
inline int i2c_master_bus_add_device(void*, const i2c_device_config_t*, void** out){*out=new int; ++live_probes; return ESP_OK;}
inline int i2c_master_bus_rm_device(void* p){if(fail_probe_delete)return ESP_FAIL;delete static_cast<int*>(p);--live_probes;return ESP_OK;}
inline int i2c_master_transmit_receive(void*,const uint8_t*,size_t,uint8_t* out,size_t,int){*out=0x68;return ESP_OK;}
