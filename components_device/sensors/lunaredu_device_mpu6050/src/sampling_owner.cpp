#include "lunaredu_device_mpu6050_sampling_owner.h"
#include "lunaredu_device_mpu6050_imu.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "kernel/periodic.h"
#include <atomic>
#include <algorithm>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::device {
namespace {
constexpr char kTag[] = "mpu6050_owner";
Mpu6050SamplingOwnerConfig owner_config;
bool scan_done = false;
bool owner_created = false;
lunaros::KernelRuntime *owner_runtime = nullptr;
std::mutex owner_mutex;
ImuBusScan boot_scan;
i2c_master_bus_handle_t scan_bus = nullptr;
i2c_master_dev_handle_t scan_device = nullptr;
bool release_scan_bus() {
  if (scan_device) {
    if (i2c_master_bus_rm_device(scan_device) != ESP_OK) {
      boot_scan.error = "i2c_scan_device_release_failed"; return false;
    }
    scan_device = nullptr;
  }
  if (scan_bus) {
    if (i2c_del_master_bus(scan_bus) != ESP_OK) {
      boot_scan.error = "i2c_scan_bus_release_failed"; return false;
    }
    scan_bus = nullptr;
  }
  return true;
}
// Runs once before the MPU driver owns the bus. Never scans during driving.
void scan_imu_bus() {
  i2c_master_bus_config_t config{};
  config.i2c_port = owner_config.sensor.i2c_port;
  config.sda_io_num = static_cast<gpio_num_t>(owner_config.sensor.sda_pin);
  config.scl_io_num = static_cast<gpio_num_t>(owner_config.sensor.scl_pin);
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  esp_err_t result = i2c_new_master_bus(&config, &bus);
  if (result != ESP_OK) {
    boot_scan.error = esp_err_to_name(result);
    return;
  }
  scan_bus = bus;
  vTaskDelay(pdMS_TO_TICKS(10));
  boot_scan.sda_idle = gpio_get_level(static_cast<gpio_num_t>(owner_config.sensor.sda_pin));
  boot_scan.scl_idle = gpio_get_level(static_cast<gpio_num_t>(owner_config.sensor.scl_pin));
  constexpr uint8_t candidates[] = {0x68, 0x69, 0x74};
  // Probe the actual candidates first. The old ascending scan could stop at
  // 0x0A without ever reaching any of them.
  std::array<uint8_t, 112> order{};
  size_t count = 0;
  for (const uint8_t address : candidates) order[count++] = address;
  for (uint8_t address = 0x08; address <= 0x77; ++address)
    if (address != 0x68 && address != 0x69 && address != 0x74)
      order[count++] = address;
  unsigned consecutive_timeouts = 0;
  for (const uint8_t address : order) {
    ++boot_scan.attempted;
    boot_scan.last_address = address;
    result = i2c_master_probe(bus, address, 100);
    if (result == ESP_ERR_TIMEOUT) {
      ++boot_scan.timeouts;
      // A stuck bus should not delay boot or OTA recovery by seconds.
      if (++consecutive_timeouts >= 3) {
        boot_scan.error = "i2c_scan_bus_timeout";
        break;
      }
    }
    if (result != ESP_ERR_TIMEOUT) consecutive_timeouts = 0;
    if (result != ESP_OK) continue;
    boot_scan.addresses[boot_scan.count++] = address;
    ESP_LOGI(kTag, "I2C ACK address=0x%02x", address);
    for (size_t i = 0; i < 3; ++i) {
      if (address != candidates[i]) continue;
      i2c_device_config_t device_config{};
      device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      device_config.device_address = address;
      device_config.scl_speed_hz = 100000;
      i2c_master_dev_handle_t device = nullptr;
      if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK)
        continue;
      scan_device = device;
      const uint8_t reg = 0x75;
      uint8_t value = 0;
      // Register pointer only; never write sensor configuration before identity.
      if (i2c_master_transmit_receive(device, &reg, 1, &value, 1, 20) == ESP_OK)
        boot_scan.identity_register_75[i] = value;
      const esp_err_t removed = i2c_master_bus_rm_device(device);
      if (removed != ESP_OK) {
        boot_scan.error = "i2c_scan_device_release_failed";
        return;
      }
      scan_device = nullptr;
    }
  }
  boot_scan.sda_after = gpio_get_level(static_cast<gpio_num_t>(owner_config.sensor.sda_pin));
  boot_scan.scl_after = gpio_get_level(static_cast<gpio_num_t>(owner_config.sensor.scl_pin));
  (void)release_scan_bus();
  boot_scan.completed = boot_scan.error.empty();
}
bool ready = false;
int64_t last_probe_us = 0;

std::atomic<bool> requested{false};
std::atomic<TaskHandle_t> owner_task{nullptr};
std::atomic<uint32_t> stop_generation{0}, stopped_generation{0};
std::mutex status_mutex;
Mpu6050SamplingStatus cached;
Mpu6050SamplingTiming timing;

void publish_status() {
  const auto sensor = device::mpu6050_status();
  std::lock_guard<std::mutex> lock(status_mutex);
  cached.sensor = sensor;
  cached.timing = timing;
  cached.bus_scan = boot_scan;
}

void probe_imu() {
  std::string error;
  last_probe_us = esp_timer_get_time();
  ready = device::setup_mpu6050(owner_config.sensor);
  if (ready) ready = device::mpu6050_set_sleep(true, error);
  const auto status = device::mpu6050_status();
  if (ready) {
    ESP_LOGI(kTag, "MPU6050 SDA=%d SCL=%d address=0x%02x WHO_AM_I=0x%02x",
             owner_config.sensor.sda_pin, owner_config.sensor.scl_pin, status.i2c_address, status.who_am_i);
  } else {
    ESP_LOGW(kTag, "optional IMU unavailable: %s",
             error.empty() ? status.last_error.c_str() : error.c_str());
  }
  publish_status();
}

void imu_worker(void *) {
  { std::lock_guard lock(owner_mutex); owner_task.store(xTaskGetCurrentTaskHandle()); }
  bool active = false;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    if (!requested.load()) {
      if (active) {
        device::mpu6050_stop_sampling();
        std::string error;
        if (!device::mpu6050_set_sleep(true, error)) ready = false;
        active = false;
        publish_status();
      }
      if (!release_scan_bus()) {
        publish_status(); ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)); continue;
      }
      // Acknowledge only after all sensor I/O from the previous demand ended.
      // Even an unavailable optional sensor must allow the app to relinquish
      // sampling; its sleep/write error remains visible in sensor diagnostics.
      if (device::mpu6050_status().initialized && !device::mpu6050_deinit()) {
        publish_status(); ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)); continue;
      }
      ready = false; last_probe_us = 0;
      publish_status();
      {
        std::lock_guard lock(owner_mutex);
        if (requested.load()) continue;
        stopped_generation.store(stop_generation.load(std::memory_order_acquire), std::memory_order_release);
        owner_task.store(nullptr, std::memory_order_release);
      }
      return; // Managed wrapper releases admission; SDK reclaims the stack.
    }
    if (!ready) {
      if ((scan_bus || scan_device) && !release_scan_bus()) {
        publish_status(); ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)); continue;
      }
      if (!scan_done && owner_config.diagnostic_scan && !owner_config.sensor.use_bsp_i2c && !owner_config.sensor.external_i2c_bus) {
        scan_done = true;
        scan_imu_bus(); boot_scan.attempts = 1;
        if (boot_scan.error != "i2c_scan_bus_release_failed" && boot_scan.error != "i2c_scan_device_release_failed") {
          const auto first = boot_scan;
          vTaskDelay(pdMS_TO_TICKS(300)); boot_scan = {};
          scan_imu_bus(); boot_scan.attempts = 2;
          boot_scan.first_sda_idle = first.sda_idle; boot_scan.first_scl_idle = first.scl_idle;
          boot_scan.first_error = first.error; boot_scan.first_count = first.count;
        }
        publish_status();
      }
      if (!requested.load() || scan_bus || scan_device) continue;
      if (!last_probe_us || esp_timer_get_time() - last_probe_us >= int64_t(owner_config.retry_interval_ms) * 1000) probe_imu();
      if (!ready) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        continue;
      }
    }
    if (!active) {
      timing = {};
      std::string error;
      if (!device::mpu6050_start_sampling(error)) {
        ready = false;
        publish_status();
        continue;
      }
      active = true;
      wake = xTaskGetTickCount();
    }
    // The task owns the fixed 20ms cadence. Calling read_now avoids a second
    // millisecond gate drifting against the RTOS deadline after minor jitter.
    const bool ok = device::mpu6050_read_now();
    if (ok) timing.record(esp_timer_get_time());
    else {
      device::mpu6050_stop_sampling();
      active = false;
      ready = false;
      last_probe_us = esp_timer_get_time();
      std::string error;
      (void)device::mpu6050_set_sleep(true, error);
    }
    publish_status();
    const TickType_t period = std::max<TickType_t>(1, pdMS_TO_TICKS(
        std::max<uint16_t>(1, 1000 / owner_config.sensor.default_sample_rate_hz)));
    wake = lunaros::periodic_sample_delay_base(xTaskGetTickCount(), wake, period);
    vTaskDelayUntil(&wake, period);
  }
}
}

bool setup_mpu6050_sampling_owner(lunaros::KernelRuntime &runtime,
    const Mpu6050SamplingOwnerConfig &config, std::string &error) {
  if (owner_created) { error = "mpu6050_owner_already_configured"; return false; }
  if (!config.sensor.default_sample_rate_hz || config.sensor.default_sample_rate_hz > 1000 || !config.retry_interval_ms) { error = "mpu6050_owner_config_invalid"; return false; }
  owner_config = config;
  owner_config.task.lifetime = lunaros::ManagedTaskLifetime::Feature;
  owner_runtime = &runtime; owner_created = true;
  error.clear();
  return true;
}

Mpu6050SamplingStatus mpu6050_sampling_status() {
  std::lock_guard<std::mutex> lock(status_mutex);
  auto result = cached;
  result.requested = requested.load();
  return result;
}

Mpu6050MotionObservation mpu6050_sampling_motion() {
  std::lock_guard<std::mutex> lock(status_mutex);
  return {cached.sensor.pose, cached.sensor.initialized,
          requested.load() && cached.sensor.sampling && !cached.sensor.sleeping,
          cached.sensor.last_error.empty()};
}

bool set_mpu6050_sampling_demand(bool needed, std::string &error) {
  error.clear();
  // Main repeats demand at the control cadence. An already expressed demand
  // needs no owner lock; a missing/retiring worker still enters normal retry.
  if(requested.load(std::memory_order_acquire)==needed &&
      (!needed || owner_task.load(std::memory_order_acquire)))return true;
  std::lock_guard lock(owner_mutex);
  if (!owner_created || !owner_runtime) {
    if (!needed) return true;
    error = "imu_owner_not_configured"; return false;
  }
  const auto task = owner_task.load();
  if (!needed || task) {
    const bool changed = requested.exchange(needed) != needed;
    if (task && changed) xTaskNotifyGive(task);
    return true;
  }
  if (owner_runtime->task_active(owner_config.task.id)) {
    error = "imu_owner_retiring"; return false;
  }
  requested.store(true);
  TaskHandle_t created = nullptr;
  if (!lunaros::create_managed_task(*owner_runtime, owner_config.task, imu_worker,
                                  nullptr, &created, error)) {
    requested.store(false);
    std::lock_guard lock(status_mutex);
    cached.sensor.last_error = "imu_owner_unavailable:" + error;
    return false;
  }
  owner_task.store(created, std::memory_order_release);
  return true;
}
bool stop_mpu6050_sampling_owner(std::string &error, uint32_t timeout_ms) {
  const auto generation = stop_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (!set_mpu6050_sampling_demand(false, error)) return false;
  const auto deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;
  do {
    if (requested.load()) { error = "imu_stop_superseded"; return false; }
    if ((!owner_runtime || !owner_runtime->task_active(owner_config.task.id)) &&
        !owner_task.load(std::memory_order_acquire) &&
        (stopped_generation.load(std::memory_order_acquire) == generation ||
         !mpu6050_sampling_status().sensor.sampling)) {
      error.clear(); return true;
    }
    vTaskDelay(1);
  } while (esp_timer_get_time() < deadline);
  error = "imu_stop_pending"; return false;
}
}
