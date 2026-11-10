#include "bsp/lunaredu_bsp.h"

#include <mutex>

#include <driver/i2c_master.h>

namespace {

constexpr const char *kI2cOwner = "bsp_i2c";
constexpr i2c_port_num_t kI2cPort = 0;
constexpr int kI2cTimeoutMs = 100;

bsp_i2c_bus_t g_active_bus = {};
bool g_i2c_claimed = false;
uint32_t g_i2c_clock_hz = 0;
i2c_master_bus_handle_t g_i2c_bus = nullptr;
i2c_master_dev_handle_t g_i2c_raw_device = nullptr;
i2c_master_dev_handle_t g_i2c_devices[128] = {};
std::recursive_mutex g_i2c_mutex;

bool is_valid_i2c_address(uint8_t address) { return address < 0x80; }

esp_err_t release_devices() {
  if (g_i2c_raw_device != nullptr) {
    const esp_err_t result = i2c_master_bus_rm_device(g_i2c_raw_device);
    if (result != ESP_OK) return result;
    g_i2c_raw_device = nullptr;
  }

  for (i2c_master_dev_handle_t &device : g_i2c_devices) {
    if (device != nullptr) {
      const esp_err_t result = i2c_master_bus_rm_device(device);
      if (result != ESP_OK) return result;
      device = nullptr;
    }
  }
  return ESP_OK;
}

i2c_master_dev_handle_t i2c_device(uint8_t address) {
  if (g_i2c_bus == nullptr || !is_valid_i2c_address(address)) {
    return nullptr;
  }

  if (g_i2c_devices[address] != nullptr) {
    return g_i2c_devices[address];
  }

  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = address;
  device_config.scl_speed_hz = g_i2c_clock_hz;

  i2c_master_dev_handle_t device = nullptr;
  const esp_err_t result =
      i2c_master_bus_add_device(g_i2c_bus, &device_config, &device);
  if (result != ESP_OK) {
    return nullptr;
  }

  g_i2c_devices[address] = device;
  return device;
}

i2c_master_dev_handle_t i2c_raw_device() {
  if (g_i2c_bus == nullptr) {
    return nullptr;
  }

  if (g_i2c_raw_device != nullptr) {
    return g_i2c_raw_device;
  }

  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
  device_config.scl_speed_hz = g_i2c_clock_hz;

  const esp_err_t result =
      i2c_master_bus_add_device(g_i2c_bus, &device_config, &g_i2c_raw_device);
  return result == ESP_OK ? g_i2c_raw_device : nullptr;
}

} // 匿名命名空间

esp_err_t bsp_i2c_init(const bsp_i2c_bus_t *bus) {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);

  if (bus == nullptr || bus->clock_hz == 0 || bus->sda_pin == bus->scl_pin ||
      bus->sda_pin >= GPIO_NUM_MAX ||
      bus->scl_pin >= GPIO_NUM_MAX ||
      !bsp_resource_contains_pin(bus->resource, bus->sda_pin) ||
      !bsp_resource_contains_pin(bus->resource, bus->scl_pin)) {
    return ESP_ERR_INVALID_ARG;
  }

  if (g_i2c_claimed) {
    const bool same = g_i2c_bus != nullptr && g_active_bus.sda_pin == bus->sda_pin &&
        g_active_bus.scl_pin == bus->scl_pin && g_active_bus.clock_hz == bus->clock_hz &&
        g_active_bus.resource == bus->resource;
    return same ? ESP_OK : ESP_ERR_INVALID_STATE;
  }

  esp_err_t result = bsp_resource_acquire(bus->resource, kI2cOwner);
  if (result != ESP_OK) {
    return result;
  }

  g_active_bus = *bus;
  g_active_bus.id = nullptr; // Only hardware fields are retained.
  g_i2c_claimed = true;
  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = kI2cPort;
  bus_config.sda_io_num = static_cast<gpio_num_t>(bus->sda_pin);
  bus_config.scl_io_num = static_cast<gpio_num_t>(bus->scl_pin);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  result = i2c_new_master_bus(&bus_config, &g_i2c_bus);
  if (result != ESP_OK) {
    if (bsp_resource_release(bus->resource, kI2cOwner) == ESP_OK) {
      g_i2c_claimed = false;
      g_active_bus = {};
    }
    return result;
  }

  g_i2c_clock_hz = bus->clock_hz;
  return ESP_OK;
}

esp_err_t bsp_i2c_deinit() {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);

  const esp_err_t devices_result = release_devices();
  if (devices_result != ESP_OK) return devices_result;

  if (g_i2c_bus != nullptr) {
    const esp_err_t result = i2c_del_master_bus(g_i2c_bus);
    if (result != ESP_OK) return result;
    g_i2c_bus = nullptr;
  }

  if (g_i2c_claimed) {
    const esp_err_t result = bsp_resource_release(g_active_bus.resource, kI2cOwner);
    if (result != ESP_OK) return result;
    g_i2c_claimed = false;
    g_active_bus = {};
  }

  g_i2c_clock_hz = 0;
  return ESP_OK;
}

bool bsp_i2c_ready() {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);
  return g_i2c_bus != nullptr;
}

i2c_master_bus_handle_t bsp_i2c_master_bus_handle() {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);
  return g_i2c_bus;
}

bool bsp_i2c_write_bytes(uint8_t address, const uint8_t *data, size_t size,
                         bool send_stop) {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);

  if (!bsp_i2c_ready() || !is_valid_i2c_address(address) ||
      (size > 0 && data == nullptr)) {
    return false;
  }

  if (size == 0 && send_stop) {
    return i2c_master_probe(g_i2c_bus, address, kI2cTimeoutMs) == ESP_OK;
  }

  if (!send_stop) {
    i2c_master_dev_handle_t raw_device = i2c_raw_device();
    if (raw_device == nullptr) {
      return false;
    }

    uint8_t address_byte = static_cast<uint8_t>(address << 1);
    i2c_operation_job_t operations[3] = {};
    size_t operation_count = 0;

    operations[operation_count++].command = I2C_MASTER_CMD_START;
    operations[operation_count].command = I2C_MASTER_CMD_WRITE;
    operations[operation_count].write.ack_check = true;
    operations[operation_count].write.data = &address_byte;
    operations[operation_count].write.total_bytes = 1;
    ++operation_count;

    if (size > 0) {
      operations[operation_count].command = I2C_MASTER_CMD_WRITE;
      operations[operation_count].write.ack_check = true;
      operations[operation_count].write.data = const_cast<uint8_t *>(data);
      operations[operation_count].write.total_bytes = size;
      ++operation_count;
    }

    return i2c_master_execute_defined_operations(raw_device, operations,
                                                 operation_count,
                                                 kI2cTimeoutMs) == ESP_OK;
  }

  i2c_master_dev_handle_t device = i2c_device(address);
  if (device == nullptr) {
    return false;
  }

  return i2c_master_transmit(device, data, size, kI2cTimeoutMs) == ESP_OK;
}

bool bsp_i2c_read_bytes(uint8_t address, uint8_t *data, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);

  if (!bsp_i2c_ready() || data == nullptr || size == 0) {
    return false;
  }

  i2c_master_dev_handle_t device = i2c_device(address);
  if (device == nullptr) {
    return false;
  }

  return i2c_master_receive(device, data, size, kI2cTimeoutMs) == ESP_OK;
}

bool bsp_i2c_write_read_bytes(uint8_t address, const uint8_t *write_data,
                              size_t write_size, uint8_t *read_data,
                              size_t read_size) {
  std::lock_guard<std::recursive_mutex> lock(g_i2c_mutex);

  if (!bsp_i2c_ready() || read_data == nullptr || read_size == 0) {
    return false;
  }
  if (write_size > 0 && write_data == nullptr) {
    return false;
  }

  i2c_master_dev_handle_t device = i2c_device(address);
  if (device == nullptr) {
    return false;
  }

  if (write_size == 0) {
    return i2c_master_receive(device, read_data, read_size, kI2cTimeoutMs) ==
           ESP_OK;
  }

  return i2c_master_transmit_receive(device, write_data, write_size, read_data,
                                     read_size, kI2cTimeoutMs) == ESP_OK;
}
