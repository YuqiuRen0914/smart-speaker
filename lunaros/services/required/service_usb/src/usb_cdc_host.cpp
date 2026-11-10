#include "lunaros_usb_host.h"
#include "lunaros_usb.h"
#include "kernel/runtime.h"
#include "kernel/managed_task.h"
#include "kernel/static_mutex.h"
#include <usb/usb_host.h>
#include <usb/cdc_acm_host.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#include <esp_attr.h>
#endif

namespace lunaredu::lunaros {
namespace {
constexpr const char *kTask = "usb.host.events";
StaticMutex state_mutex, io_mutex;
UsbHostSnapshot state;
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
// CDC data callbacks run on the SDK task, never in the USB ISR. Keep the ring
// in fitted PSRAM while mutexes, ISR/notification state and DMA stay internal.
EXT_RAM_BSS_ATTR
#endif
std::array<uint8_t, 8192> rx;
size_t rx_begin = 0, rx_count = 0;
std::atomic<bool> retiring{false}, disconnected{false};
KernelRuntime *runtime = nullptr;
DeviceProjectionRegistry *projection = nullptr;
cdc_acm_dev_hdl_t device = nullptr;
bool host_installed = false, cdc_installed = false;
StaticSemaphore_t rx_ready_storage;
SemaphoreHandle_t rx_ready = nullptr;
bool data_received(const uint8_t *data, size_t size, void *) {
  StaticLockGuard lock(state_mutex);
  const size_t accepted = std::min(size, rx.size() - rx_count);
  const auto start = (rx_begin + rx_count) % rx.size();
  const auto first = std::min(accepted, rx.size() - start);
  if (accepted) {
    std::memcpy(rx.data() + start, data, first);
    std::memcpy(rx.data(), data + first, accepted - first);
  }
  rx_count += accepted;
  state.dropped_bytes += size - accepted;
  if (rx_ready) (void)xSemaphoreGive(rx_ready);
  // The callback owns the complete packet; an overflow is counted and makes
  // protocol I/O fail. Do not ask CDC to append the same packet a second time.
  return true;
}
void event_received(const cdc_acm_host_dev_event_data_t *event, void *) {
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
    disconnected = true;
    StaticLockGuard lock(state_mutex); state.connected = false; ++state.generation;
    rx_count = rx_begin = 0;
    if (rx_ready) (void)xSemaphoreGive(rx_ready);
  }
  if (event->type == CDC_ACM_HOST_ERROR) {
    disconnected = true;
    StaticLockGuard lock(state_mutex); state.error = event->data.error;
    state.connected = false; ++state.generation; rx_count = rx_begin = 0;
    if (rx_ready) (void)xSemaphoreGive(rx_ready);
  }
}
void events(void *) {
  usb_host_config_t host_config{};
  host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  int result = usb_host_install(&host_config);
  host_installed = result == ESP_OK;
  if (host_installed) {
    const cdc_acm_host_driver_config_t config = {
      .driver_task_stack_size = 4096, .driver_task_priority = 5, .xCoreID = 0, .new_dev_cb = nullptr};
    result = cdc_acm_host_install(&config); cdc_installed = result == ESP_OK;
  }
  { StaticLockGuard lock(state_mutex); state.running = cdc_installed; state.error = result; }
  while (!retiring) {
    if (!cdc_installed) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    uint32_t flags = 0;
    (void)usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) (void)usb_host_device_free_all();
    if (disconnected) {
      StaticLockGuard io(io_mutex);
      if (!device || cdc_acm_host_close(device) == ESP_OK) {
        device = nullptr; disconnected = false;
      }
      StaticLockGuard lock(state_mutex);
      state.connected = false; ++state.generation; rx_count = rx_begin = 0;
    }
    if (!device && !retiring) {
      const cdc_acm_host_device_config_t config = {
        .connection_timeout_ms = 50, .out_buffer_size = 4096, .in_buffer_size = 512,
        .event_cb = event_received, .data_cb = data_received, .user_arg = nullptr};
      cdc_acm_dev_hdl_t candidate = nullptr;
      if (cdc_acm_host_open(0x303a, 0x1001, 0, &config, &candidate) == ESP_OK) {
        StaticLockGuard io(io_mutex); device = candidate;
        StaticLockGuard lock(state_mutex);
        state.connected = true; state.error = 0; ++state.generation;
        state.dropped_bytes = 0; rx_count = rx_begin = 0;
      }
    }
  }
  // Keep ownership if teardown fails; stop callers can keep polling. The
  // callback context and rings remain alive until every driver has retired.
  for (;;) {
    { StaticLockGuard io(io_mutex);
      if (device && cdc_acm_host_close(device) == ESP_OK) device = nullptr;
    }
    if (!device && cdc_installed && cdc_acm_host_uninstall() == ESP_OK) cdc_installed = false;
    if (!device && !cdc_installed && host_installed) {
      uint32_t flags = 0;
      (void)usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
      (void)usb_host_device_free_all();
      if (usb_host_uninstall() == ESP_OK) host_installed = false;
    }
    if (!device && !cdc_installed && !host_installed) break;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  StaticLockGuard lock(state_mutex); state.running = state.connected = false;
  rx_count = rx_begin = 0; ++state.generation;
}
}
bool start_usb_cdc_host(KernelRuntime &owner, DeviceProjectionRegistry &registry, std::string &error) {
  if (runtime) { error = "usb_host_owned"; return false; }
  if (!suspend_usb_device_for_host(registry, error)) return false;
  runtime = &owner; projection = &registry; retiring = disconnected = false;
  if (!rx_ready) rx_ready = xSemaphoreCreateBinaryStatic(&rx_ready_storage);
  (void)xSemaphoreTake(rx_ready, 0);
  { StaticLockGuard lock(state_mutex);
    const auto generation = state.generation + 1;
    state = {}; state.generation = generation; rx_count = rx_begin = 0;
  }
  const ManagedTaskDefinition task{.id = kTask, .owner_service_id = kUsbServiceId,
    .role = "host-events", .stack_bytes = 4096, .priority = 4, .core_affinity = 0,
    .lifetime = ManagedTaskLifetime::Feature};
  ManagedTaskHandle handle = nullptr;
  if (!create_managed_task(owner, task, events, nullptr, &handle, error)) {
    std::string restore;
    if (resume_usb_device_after_host(registry, restore)) {
      runtime = nullptr; projection = nullptr;
    } else error += ": " + restore;
    return false;
  }
  error.clear(); return true;
}
bool stop_usb_cdc_host(std::string &error) {
  if (!runtime) { error.clear(); return true; }
  retiring = true;
  if (runtime->task_active(kTask)) { error = "usb_host_stopping"; return false; }
  if (!resume_usb_device_after_host(*projection, error)) return false;
  runtime = nullptr; projection = nullptr; error.clear(); return true;
}
void usb_cdc_host_snapshot_into(UsbHostSnapshot &out) { StaticLockGuard lock(state_mutex); out = state; }
bool usb_cdc_host_wait_rx(uint32_t generation, uint32_t timeout_ms) {
  {
    StaticLockGuard lock(state_mutex);
    if (!state.connected || state.generation != generation || state.error || retiring) return false;
    if (rx_count) return true;
  }
  if (!rx_ready || xSemaphoreTake(rx_ready, std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms))) != pdTRUE) return false;
  StaticLockGuard lock(state_mutex);
  return state.connected && state.generation == generation && !state.error && !retiring && rx_count;
}
size_t usb_cdc_host_read(uint32_t generation, uint8_t *data, size_t capacity) {
  StaticLockGuard lock(state_mutex);
  if (!state.connected || generation != state.generation) return 0;
  const auto count = std::min(capacity, rx_count);
  const auto first = std::min(count, rx.size() - rx_begin);
  if (count) {
    std::memcpy(data, rx.data() + rx_begin, first);
    std::memcpy(data + first, rx.data(), count - first);
  }
  rx_begin = (rx_begin + count) % rx.size(); rx_count -= count; return count;
}
bool usb_cdc_host_write(uint32_t generation, const uint8_t *data, size_t size, uint32_t timeout_ms) {
  StaticLockGuard lock(io_mutex);
  { StaticLockGuard state_lock(state_mutex);
    if (!state.connected || state.generation != generation || state.error) return false;
  }
  return device && !disconnected && !retiring &&
      cdc_acm_host_data_tx_blocking(device, data, size, timeout_ms) == ESP_OK;
}
bool usb_cdc_host_control(uint32_t generation, bool dtr, bool rts) {
  StaticLockGuard lock(io_mutex);
  { StaticLockGuard state_lock(state_mutex);
    if (!state.connected || state.generation != generation || state.error) return false;
  }
  return device && !disconnected && !retiring && cdc_acm_host_set_control_line_state(device, dtr, rts) == ESP_OK;
}
void usb_cdc_host_clear_rx(uint32_t generation) {
  StaticLockGuard lock(state_mutex); if (state.generation == generation) rx_count = rx_begin = 0;
}
} // namespace lunaredu::lunaros
