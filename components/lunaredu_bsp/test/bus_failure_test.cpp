#include <cassert>
#include <cstdio>
#include <cstring>
#include <esp_vfs_fat.h>
#include "bsp/lunaredu_bsp.h"
const char *esp_err_to_name(esp_err_t) { return "injected_error"; }
int main(){
 bsp_resource_reset_all();
 auto bus=*bsp_expansion_i2c1_bus();
 assert(bsp_i2c_init(&bus)==ESP_OK);
 assert(bsp_i2c_init(bsp_mpu_i2c_bus())==ESP_ERR_INVALID_STATE && i2c_buses_live==1);
 bus.resource=BSP_RESOURCE_TFCARD; // The active descriptor must be an owned copy.
 uint8_t byte=1;assert(bsp_i2c_write_bytes(0x30,&byte,1,true));
 fail_i2c_remove=true;assert(bsp_i2c_deinit()==ESP_ERR_INVALID_STATE);
 assert(i2c_devices_live==1 && i2c_buses_live==1 && bsp_i2c_ready());
 fail_i2c_remove=false;fail_i2c_delete=true;
 assert(bsp_i2c_deinit()==ESP_ERR_INVALID_STATE);
 assert(i2c_devices_live==0 && i2c_buses_live==1 && bsp_resource_is_acquired(BSP_RESOURCE_EXPANSION_I2C1));
 fail_i2c_delete=false;assert(bsp_i2c_deinit()==ESP_OK);
 assert(i2c_buses_live==0 && !bsp_resource_is_acquired(BSP_RESOURCE_EXPANSION_I2C1));
 char path[]="/test-card";assert(bsp_sdcard_mount(path)==ESP_OK);path[1]='X';
 assert(std::strcmp(bsp_sdcard_get_status().mount_path,"/test-card")==0);
 assert(bsp_sdcard_mount("/different")==ESP_ERR_INVALID_STATE);
 fail_sd_unmount=true;assert(bsp_sdcard_unmount()==ESP_ERR_INVALID_STATE);
 assert(bsp_sdcard_get_status().mounted && cards_live==1 && bsp_resource_is_acquired(BSP_RESOURCE_TFCARD));
 fail_sd_unmount=false;assert(bsp_sdcard_unmount()==ESP_OK && cards_live==0);
 assert(!bsp_sdcard_get_status().mounted && !bsp_resource_is_acquired(BSP_RESOURCE_TFCARD));
 puts("PASS BSP I2C device/bus teardown retry, descriptor ownership and SD unmount retry");
}
