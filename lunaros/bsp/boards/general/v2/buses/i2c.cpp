#include "bsp/boards/general/v2.h"
#include <mutex>

namespace {
struct Bus {
  std::mutex mutex;
  i2c_master_bus_handle_t handle=nullptr;
  i2c_master_dev_handle_t devices[128]{};
  bool claimed=false, closing=false;
};
Bus buses[2];
const char *owners[]={"v2.i2c.common","v2.i2c.interface"};
int index_of(bsp_v2_i2c_t id) {
  const int i=static_cast<int>(id); return i>=0 && i<2 ? i : -1;
}
const bsp_v2_i2c_bus_t *descriptor(int i) {
  const auto *b=bsp_v2_board();
  return !b || i<0 ? nullptr : i==0 ? &b->common_i2c : &b->interface_i2c;
}
bool valid_address(uint8_t a) { return a>=0x08 && a<=0x77; }
// QMC6309 intentionally uses seven-bit 0x7c, despite the reserved range.
bool accepted_address(uint8_t a) { return valid_address(a) || a==0x7c; }
bool valid_timeout(int t) { return t>0 && t<=1000; }
}

esp_err_t bsp_v2_i2c_init(bsp_v2_i2c_t id) {
  const int i=index_of(id); if(i<0) return ESP_ERR_INVALID_ARG;
  const auto *d=descriptor(i); if(!d) return ESP_ERR_NOT_SUPPORTED;
  auto &b=buses[i]; std::lock_guard<std::mutex> lock(b.mutex);
  if(b.closing) return ESP_ERR_INVALID_STATE;
  if(b.handle) return ESP_OK;
  if(b.claimed) return ESP_ERR_INVALID_STATE;
  auto e=bsp_v2_resource_acquire(d->resource,owners[i]); if(e!=ESP_OK) return e;
  b.claimed=true;
  i2c_master_bus_config_t cfg{};
  cfg.i2c_port=static_cast<i2c_port_num_t>(i);
  cfg.sda_io_num=static_cast<gpio_num_t>(d->sda_pin);
  cfg.scl_io_num=static_cast<gpio_num_t>(d->scl_pin);
  cfg.clk_source=I2C_CLK_SRC_DEFAULT; cfg.glitch_ignore_cnt=7;
  // The board has external pull-ups; do not assume their final strength.
  cfg.flags.enable_internal_pullup=false;
  e=i2c_new_master_bus(&cfg,&b.handle);
  if(e!=ESP_OK && bsp_v2_resource_release(d->resource,owners[i])==ESP_OK) b.claimed=false;
  return e;
}
esp_err_t bsp_v2_i2c_deinit(bsp_v2_i2c_t id) {
  const int i=index_of(id); if(i<0) return ESP_ERR_INVALID_ARG;
  const auto *d=descriptor(i); if(!d) return ESP_ERR_NOT_SUPPORTED;
  auto &b=buses[i]; std::lock_guard<std::mutex> lock(b.mutex);
  b.closing=true;
  for(auto &device:b.devices) {
    if(!device) continue;
    const auto e=i2c_master_bus_rm_device(device); if(e!=ESP_OK) return e;
    device=nullptr;
  }
  if(b.handle) {
    const auto e=i2c_del_master_bus(b.handle); if(e!=ESP_OK) return e;
    b.handle=nullptr;
  }
  if(b.claimed) {
    const auto e=bsp_v2_resource_release(d->resource,owners[i]); if(e!=ESP_OK) return e;
    b.claimed=false;
  }
  b.closing=false; return ESP_OK;
}
bool bsp_v2_i2c_ready(bsp_v2_i2c_t id) {
  const int i=index_of(id); if(i<0 || !descriptor(i)) return false;
  auto &b=buses[i]; std::lock_guard<std::mutex> lock(b.mutex);
  return b.handle && !b.closing;
}
i2c_master_bus_handle_t bsp_v2_i2c_handle(bsp_v2_i2c_t id) {
  const int i=index_of(id); if(i<0 || !descriptor(i)) return nullptr;
  auto &b=buses[i]; std::lock_guard<std::mutex> lock(b.mutex);
  return b.closing ? nullptr : b.handle;
}
esp_err_t bsp_v2_i2c_probe(bsp_v2_i2c_t id,uint8_t address,int timeout) {
  const int i=index_of(id); if(i<0 || !accepted_address(address) || !valid_timeout(timeout)) return ESP_ERR_INVALID_ARG;
  if(!descriptor(i)) return ESP_ERR_NOT_SUPPORTED;
  auto &b=buses[i]; std::lock_guard<std::mutex> lock(b.mutex);
  if(!b.handle || b.closing) return ESP_ERR_INVALID_STATE;
  return i2c_master_probe(b.handle,address,timeout);
}
esp_err_t bsp_v2_i2c_transfer(bsp_v2_i2c_t id,uint8_t address,
    const uint8_t *write_data,size_t write_size,uint8_t *read_data,size_t read_size,int timeout) {
  const int i=index_of(id);
  if(i<0 || !accepted_address(address) || !valid_timeout(timeout) ||
      (!write_size && !read_size) || (write_size && !write_data) || (read_size && !read_data)) return ESP_ERR_INVALID_ARG;
  const auto *d=descriptor(i); if(!d) return ESP_ERR_NOT_SUPPORTED;
  auto &b=buses[i]; std::lock_guard<std::mutex> lock(b.mutex);
  if(!b.handle || b.closing) return ESP_ERR_INVALID_STATE;
  auto &device=b.devices[address];
  if(!device) {
    i2c_device_config_t cfg{}; cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7;
    cfg.device_address=address; cfg.scl_speed_hz=d->clock_hz;
    const auto e=i2c_master_bus_add_device(b.handle,&cfg,&device); if(e!=ESP_OK) return e;
  }
  if(!read_size) return i2c_master_transmit(device,write_data,write_size,timeout);
  if(!write_size) return i2c_master_receive(device,read_data,read_size,timeout);
  return i2c_master_transmit_receive(device,write_data,write_size,read_data,read_size,timeout);
}
