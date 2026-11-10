#pragma once
#include "driver/i2c_master.h"
using i2c_bus_handle_t=void*; using i2c_port_t=int;
constexpr int I2C_MODE_MASTER=0,GPIO_PULLUP_ENABLE=1;
struct i2c_config_t {int mode,sda_io_num,sda_pullup_en,scl_io_num,scl_pullup_en; struct {unsigned clk_speed;} master; int clk_flags;};
inline int live_buses=0, bus_creates=0, remaining_devices=0; inline bool fail_bus_delete=false;
inline void* i2c_bus_create(int,const i2c_config_t*){++live_buses;++bus_creates;return new int;}
inline int i2c_bus_delete(void** p){if(fail_bus_delete)return ESP_FAIL;delete static_cast<int*>(*p);*p=nullptr;--live_buses;return ESP_OK;}
inline unsigned i2c_bus_get_created_device_num(void*){return remaining_devices;}
inline void* i2c_bus_get_internal_bus_handle(void* bus){return bus;}
