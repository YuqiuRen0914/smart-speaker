#include "smart_speaker_sensors.h"

#include <algorithm>
#include <cstdio>
#include <mutex>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lunaredu_device_mpu6050_imu.h"
#include "sdkconfig.h"

namespace smart_speaker {
namespace {

constexpr const char *kTag = "speaker_sensors";
constexpr uint8_t kTouchAddresses[] = {0x38, 0x37};
constexpr uint8_t kTouchRegData = 0x00;
constexpr uint32_t kTouchClockHz = 100000;
constexpr uint32_t kMpuClockHz = 100000;
constexpr uint8_t kMpuRegisterWhoAmI = 0x75;
constexpr uint32_t kTouchPollMs = 20;

std::mutex g_mutex;
i2c_master_bus_handle_t g_mpu_bus = nullptr;
i2c_master_bus_handle_t g_touch_bus = nullptr;
i2c_master_dev_handle_t g_touch_device = nullptr;
bool g_setup_attempted = false;
bool g_imu_running = false;
lunaredu::services::imu::ImuService *g_imu_service = nullptr;
uint32_t g_last_touch_poll_ms = 0;
SmartSpeakerMpuSnapshot g_mpu;
SmartSpeakerTouchSnapshot g_touch;
uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

esp_err_t create_bus(i2c_port_num_t port, int sda, int scl, uint32_t clock,
                     i2c_master_bus_handle_t *out) {
  if (out == nullptr || sda < 0 || scl < 0 || sda == scl) {
    return ESP_ERR_INVALID_ARG;
  }
  if (*out != nullptr) return ESP_OK;
  i2c_master_bus_config_t config = {};
  config.i2c_port = port;
  config.sda_io_num = static_cast<gpio_num_t>(sda);
  config.scl_io_num = static_cast<gpio_num_t>(scl);
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  const esp_err_t result = i2c_new_master_bus(&config, out);
  if (result == ESP_OK) {
    ESP_LOGI(kTag, "I2C%d ready SDA=%d SCL=%d clock=%lu",
             static_cast<int>(port), sda, scl,
             static_cast<unsigned long>(clock));
  }
  return result;
}

std::string format_addresses(const SmartSpeakerMpuSnapshot &snapshot) {
  if (snapshot.scan_count == 0) return "none";
  std::string result;
  char item[8] = {};
  const uint8_t count = std::min<uint8_t>(
      snapshot.scan_count, kSmartSpeakerI2cScanMaxAddresses);
  for (uint8_t index = 0; index < count; ++index) {
    if (!result.empty()) result += ",";
    std::snprintf(item, sizeof(item), "0x%02x",
                  snapshot.scan_addresses[index]);
    result += item;
  }
  return result;
}

void scan_mpu_bus_locked() {
  g_mpu.scan_complete = false;
  g_mpu.scan_count = 0;
  g_mpu.scan_addresses = {};
  if (g_mpu_bus == nullptr) return;
  constexpr uint8_t kMpuAddresses[] = {0x68, 0x69};
  for (const uint8_t address : kMpuAddresses) {
    if (i2c_master_probe(g_mpu_bus, address, 100) != ESP_OK) continue;
    if (g_mpu.scan_count < kSmartSpeakerI2cScanMaxAddresses) {
      g_mpu.scan_addresses[g_mpu.scan_count] = address;
    }
    ++g_mpu.scan_count;
  }
  g_mpu.scan_complete = true;
  ESP_LOGI(kTag, "MPU I2C scan SDA=%d SCL=%d count=%u addresses=%s",
           CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SDA_GPIO,
           CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SCL_GPIO,
           static_cast<unsigned>(g_mpu.scan_count),
           format_addresses(g_mpu).c_str());
}

uint8_t preferred_mpu_address_from_scan_locked() {
  const uint8_t count = std::min<uint8_t>(
      g_mpu.scan_count, kSmartSpeakerI2cScanMaxAddresses);
  for (uint8_t index = 0; index < count; ++index) {
    if (g_mpu.scan_addresses[index] == 0x68 ||
        g_mpu.scan_addresses[index] == 0x69) {
      return g_mpu.scan_addresses[index];
    }
  }
  return 0x68;
}

void read_mpu_identity_locked(uint8_t address) {
  g_mpu.who_am_i_valid = false;
  g_mpu.who_am_i = 0;
  g_mpu.who_am_i_error.clear();
  if (g_mpu_bus == nullptr || address == 0) {
    g_mpu.who_am_i_error = "mpu_i2c_bus_unavailable";
    return;
  }

  i2c_master_dev_handle_t device = nullptr;
  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = address;
  device_config.scl_speed_hz = kMpuClockHz;
  esp_err_t result =
      i2c_master_bus_add_device(g_mpu_bus, &device_config, &device);
  if (result != ESP_OK || device == nullptr) {
    g_mpu.who_am_i_error = std::string("who_am_i_device_failed:") +
                           esp_err_to_name(result);
    return;
  }

  uint8_t reg = kMpuRegisterWhoAmI;
  uint8_t value = 0;
  result = i2c_master_transmit_receive(device, &reg, 1, &value, 1, 100);
  const esp_err_t remove_result = i2c_master_bus_rm_device(device);
  if (remove_result != ESP_OK && result == ESP_OK) result = remove_result;
  if (result != ESP_OK) {
    g_mpu.who_am_i_error = std::string("who_am_i_read_failed:") +
                           esp_err_to_name(result);
    return;
  }

  g_mpu.who_am_i_valid = true;
  g_mpu.who_am_i = value;
  ESP_LOGI(kTag, "MPU address=0x%02x WHO_AM_I=0x%02x", address, value);
}

void set_touch_error_locked(const std::string &error) {
  g_touch.last_error = error;
  if (!error.empty()) ESP_LOGW(kTag, "touch: %s", error.c_str());
}

bool setup_touch_locked() {
  const esp_err_t bus = create_bus(
      I2C_NUM_1, CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_SDA_GPIO,
      CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_SCL_GPIO, kTouchClockHz,
      &g_touch_bus);
  if (bus != ESP_OK) {
    set_touch_error_locked(std::string("touch_i2c_init_failed:") +
                           esp_err_to_name(bus));
    return false;
  }
  if (CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_INT_GPIO >= 0) {
    gpio_config_t irq = {};
    irq.pin_bit_mask =
        1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_INT_GPIO;
    irq.mode = GPIO_MODE_INPUT;
    irq.pull_up_en = GPIO_PULLUP_ENABLE;
    irq.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irq.intr_type = GPIO_INTR_DISABLE;
    (void)gpio_config(&irq);
  }
  for (const uint8_t address : kTouchAddresses) {
    if (i2c_master_probe(g_touch_bus, address, 100) != ESP_OK) continue;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = address;
    device.scl_speed_hz = kTouchClockHz;
    if (i2c_master_bus_add_device(g_touch_bus, &device, &g_touch_device) ==
        ESP_OK) {
      g_touch.initialized = true;
      g_touch.address = address;
      set_touch_error_locked("");
      ESP_LOGI(kTag, "FT6236-compatible touch ready address=0x%02x", address);
      return true;
    }
  }
  set_touch_error_locked("touch_controller_not_found");
  return false;
}

void sample_touch_locked() {
  if (!g_touch.initialized || g_touch_device == nullptr) return;
  uint8_t reg = kTouchRegData;
  uint8_t data[15] = {};
  const esp_err_t result =
      i2c_master_transmit_receive(g_touch_device, &reg, 1, data, sizeof(data),
                                  100);
  if (result != ESP_OK) {
    g_touch.pressed = false;
    g_touch.contact_count = 0;
    set_touch_error_locked(std::string("touch_read_failed:") +
                           esp_err_to_name(result));
    return;
  }
  const uint8_t touch_count = std::min<uint8_t>(data[2] & 0x0f, 2);
  bool pressed = false;
  uint16_t x = g_touch.x;
  uint16_t y = g_touch.y;
  if (touch_count > 0) {
    const uint8_t event = static_cast<uint8_t>((data[3] >> 6) & 0x03);
    pressed = event == 0 || event == 2;
    x = static_cast<uint16_t>(((data[3] & 0x0f) << 8) | data[4]);
    y = static_cast<uint16_t>(((data[5] & 0x0f) << 8) | data[6]);
    x = std::min<uint16_t>(x, 239);
    y = std::min<uint16_t>(y, 239);
  }
  g_touch.pressed = pressed;
  g_touch.contact_count = pressed?touch_count:0;
  g_touch.x = x;
  g_touch.y = y;
  ++g_touch.sample_count;
  g_touch.sampled_at_ms = now_ms();
  set_touch_error_locked("");
}

bool setup_mpu_locked() {
  const esp_err_t bus = create_bus(
      I2C_NUM_0, CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SDA_GPIO,
      CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SCL_GPIO, kMpuClockHz,
      &g_mpu_bus);
  if (bus != ESP_OK) {
    g_mpu.last_error = std::string("mpu_i2c_init_failed:") +
                       esp_err_to_name(bus);
    ESP_LOGW(kTag, "%s", g_mpu.last_error.c_str());
    return false;
  }
  scan_mpu_bus_locked();
  const uint8_t preferred_address = preferred_mpu_address_from_scan_locked();
  read_mpu_identity_locked(preferred_address);
  const SmartSpeakerMpuSnapshot scan = g_mpu;
  lunaredu::device::Mpu6050ModuleConfig config;
  config.sda_pin = CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SDA_GPIO;
  config.scl_pin = CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SCL_GPIO;
  config.i2c_clock_hz = kMpuClockHz;
  config.use_bsp_i2c = false;
  config.external_i2c_bus = g_mpu_bus;
  config.preferred_address = preferred_address;
  config.allow_secondary_address_fallback = true;
  config.default_sample_rate_hz = 50;
  config.enable_pose = true;
  if (!lunaredu::device::setup_mpu6050_with_bus(config, g_mpu_bus)) {
    g_mpu = scan;
    g_mpu.last_error = "mpu6050_setup_failed scan=" + format_addresses(g_mpu);
    ESP_LOGW(kTag, "%s", g_mpu.last_error.c_str());
    return false;
  }
  std::string error;
  if (!lunaredu::device::mpu6050_start_sampling(error)) {
    g_mpu.last_error = error.empty() ? "mpu_sampling_start_failed" : error;
    ESP_LOGW(kTag, "%s", g_mpu.last_error.c_str());
    return false;
  }
  const auto status = lunaredu::device::mpu6050_status();
  if (status.i2c_address != 0 && g_mpu.scan_count == 0) {
    g_mpu.scan_addresses[0] = status.i2c_address;
    g_mpu.scan_count = 1;
    g_mpu.scan_complete = true;
  }
  if (status.who_am_i != 0) {
    g_mpu.who_am_i_valid = true;
    g_mpu.who_am_i = status.who_am_i;
    g_mpu.who_am_i_error.clear();
  }
  ESP_LOGI(kTag, "MPU6050 sampling ready SDA=%d SCL=%d",
           CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SDA_GPIO,
           CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SCL_GPIO);
  return true;
}

void update_mpu_snapshot_locked() {
  lunaredu::device::tick_mpu6050();
  const auto status = lunaredu::device::mpu6050_status();
  g_mpu.initialized = status.initialized;
  g_mpu.sampling = status.sampling;
  g_mpu.address = status.i2c_address;
  g_mpu.temperature_c = status.latest.temperature_c;
  g_mpu.pose_valid = status.pose.valid;
  g_mpu.angle_x = status.pose.angle_x;
  g_mpu.angle_y = status.pose.angle_y;
  g_mpu.angle_z = status.pose.angle_z;
  g_mpu.last_error = status.last_error;
  if (status.who_am_i != 0) {
    g_mpu.who_am_i_valid = true;
    g_mpu.who_am_i = status.who_am_i;
    g_mpu.who_am_i_error.clear();
  }
  if (!status.initialized && g_mpu.scan_complete && !g_mpu.last_error.empty()) {
    g_mpu.last_error += " scan=" + format_addresses(g_mpu);
  }

}

bool stop_mpu_locked(std::string &error) {
  lunaredu::device::mpu6050_stop_sampling();
  g_imu_running = false;
  if (!lunaredu::device::mpu6050_deinit()) {
    error = "mpu6050_deinit_failed";
    return false;
  }
  if (g_mpu_bus != nullptr) {
    const esp_err_t result = i2c_del_master_bus(g_mpu_bus);
    if (result != ESP_OK) {
      error = std::string("mpu_i2c_release_failed:") + esp_err_to_name(result);
      return false;
    }
    g_mpu_bus = nullptr;
  }
  g_mpu = {};
  error.clear();
  return true;
}

} // namespace

bool setup_smart_speaker_sensors() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_setup_attempted) return g_touch.initialized;
  g_setup_attempted = true;
  return setup_touch_locked();
}

void tick_smart_speaker_sensors() {
  if (g_imu_service != nullptr) {
    lunaredu::services::imu::ImuSample sample;
    std::string error;
    (void)g_imu_service->snapshot(sample, error);
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_setup_attempted) return;
  const uint32_t now = now_ms();
  if (now - g_last_touch_poll_ms >= kTouchPollMs) {
    g_last_touch_poll_ms = now;
    sample_touch_locked();
  }
}

void set_smart_speaker_imu_service(
    lunaredu::services::imu::ImuService *service) {
  g_imu_service = service;
}

bool start_smart_speaker_imu(std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_imu_running) { error.clear(); return true; }
  if (!setup_mpu_locked()) {
    error = g_mpu.last_error.empty() ? "mpu6050_setup_failed" : g_mpu.last_error;
    std::string cleanup_error;
    if (!stop_mpu_locked(cleanup_error)) error += ";" + cleanup_error;
    g_mpu.last_error = error;
    return false;
  }
  // The IMU service probes its source during start. Allow the 50 Hz driver
  // several sample intervals before deciding whether the sensor is ready.
  for (unsigned attempt = 0; attempt < 6; ++attempt) {
    update_mpu_snapshot_locked();
    const auto status = lunaredu::device::mpu6050_status();
    if (status.ok && status.latest.valid) {
      g_imu_running = true;
      error.clear();
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  error = g_mpu.last_error.empty() ? "mpu6050_first_sample_missing"
                                   : g_mpu.last_error;
  std::string cleanup_error;
  if (!stop_mpu_locked(cleanup_error)) error += ";" + cleanup_error;
  g_mpu.last_error = error;
  return false;
}

bool read_smart_speaker_imu(lunaredu::services::imu::ImuSample &sample,
                            std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_imu_running) { error = "mpu6050_not_running"; return false; }
  update_mpu_snapshot_locked();
  const auto status = lunaredu::device::mpu6050_status();
  if (!status.ok || !status.latest.valid) {
    error = status.last_error.empty() ? "mpu6050_read_failed" : status.last_error;
    return false;
  }
  sample.accel_g = {status.latest.accel_g_x, status.latest.accel_g_y,
                    status.latest.accel_g_z};
  sample.gyro_dps = {status.latest.gyro_dps_x, status.latest.gyro_dps_y,
                     status.latest.gyro_dps_z};
  error.clear();
  return true;
}

bool stop_smart_speaker_imu(std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return stop_mpu_locked(error);
}

SmartSpeakerMpuSnapshot smart_speaker_mpu_snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_mpu;
}

SmartSpeakerTouchSnapshot smart_speaker_touch_snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_touch;
}



} // namespace smart_speaker
