#pragma once
#include "driver/i2c_master.h"
using mpu6050_handle_t=void*;
inline bool fail_sensor_delete=false; inline int live_sensors=0;
struct mpu6050_config_t {int i2c_address,i2c_clock_speed,low_pass_filter,gyro_clock_source,gyro_full_scale_range,accel_full_scale_range,irq_io_num,irq_io_active_level,irq_io_mode,irq_latch,irq_clear_behavior;};
struct mpu6050_who_am_i_register_t {int reg=0x68;};
struct Register {struct {int low_pass_filter,ext_sync_setting,full_scale_range,clock_source;bool sleep_enabled,cycle_enabled,temp_disabled,reset_enabled;} bits;};
using mpu6050_config_register_t=Register;using mpu6050_gyro_config_register_t=Register;using mpu6050_accel_config_register_t=Register;using mpu6050_power_management1_register_t=Register;
struct Axes {float x_axis=0,y_axis=0,z_axis=0;}; using mpu6050_gyro_data_axes_t=Axes;using mpu6050_accel_data_axes_t=Axes;
inline int mpu6050_init(void*,const mpu6050_config_t*,void** out){*out=new int;++live_sensors;return ESP_OK;}
inline int mpu6050_delete(void* sensor){if(fail_sensor_delete)return ESP_FAIL;delete static_cast<int*>(sensor);--live_sensors;return ESP_OK;}
inline int mpu6050_get_who_am_i_register(void*,mpu6050_who_am_i_register_t* out){out->reg=0x68;return ESP_OK;}
inline int mpu6050_get_motion(void*,Axes* gyro,Axes* accel,float* temp){*gyro={1,2,3};*accel={0,0,1};*temp=25;return ESP_OK;}
constexpr int MPU6050_ACCEL_FS_RANGE_2G=0;
constexpr int MPU6050_DIGITAL_LP_FILTER_ACCEL_44KHZ_GYRO_42KHZ=0;
constexpr int MPU6050_EXT_SYNC_SETTING_INPUT_DISABLED=0;
constexpr int MPU6050_GYRO_CS_PLL_X_AXIS_REF=0;
constexpr int MPU6050_GYRO_FS_RANGE_250DPS=0;
constexpr int MPU6050_IRQ_CLEAR_ON_STATUS_READ=0;
constexpr int MPU6050_IRQ_LATCH_50US=0;
constexpr int MPU6050_IRQ_PIN_ACTIVE_HIGH=0;
constexpr int MPU6050_IRQ_PIN_PUSH_PULL=0;
template<class... T> int mpu6050_set_accel_config_register(T...){return ESP_OK;}
template<class... T> int mpu6050_set_config_register(T...){return ESP_OK;}
template<class... T> int mpu6050_set_gyro_config_register(T...){return ESP_OK;}
template<class... T> int mpu6050_set_power_management1_register(T...){return ESP_OK;}
template<class... T> int mpu6050_set_sample_rate_divider_register(T...){return ESP_OK;}
