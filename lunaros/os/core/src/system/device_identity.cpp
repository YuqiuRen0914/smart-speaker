#include "system/identity.h"

#include <atomic>
#include <cstdio>

#include "kernel/static_mutex.h"
#include "platform/system.h"

namespace {

char g_hardware_serial_number[32] = {};
lunaredu::lunaros::StaticMutex g_identity_mutex;
std::atomic<bool> g_identity_initialized{false};

void initialize_hardware_serial_number() {
  uint8_t mac[6] = {0};
  if (!lunaredu::lunaros::system_read_factory_mac(mac, sizeof(mac))) {
    std::snprintf(g_hardware_serial_number, sizeof(g_hardware_serial_number),
                  "LUNAR-000000000000");
    return;
  }

  uint64_t chip_id = 0;
  for (uint8_t byte : mac) {
    chip_id = (chip_id << 8) | byte;
  }

  std::snprintf(g_hardware_serial_number, sizeof(g_hardware_serial_number),
                "LUNAR-%04X%08lX",
                static_cast<unsigned>(static_cast<uint16_t>(chip_id >> 32)),
                static_cast<unsigned long>(chip_id & 0xFFFFFFFFULL));
}

} // 匿名命名空间

namespace lunaredu::lunaros {

const char *hardware_serial_number() {
  if (!g_identity_initialized.load(std::memory_order_acquire)) {
    StaticLockGuard lock(g_identity_mutex);
    if (!g_identity_initialized.load(std::memory_order_relaxed)) {
      initialize_hardware_serial_number();
      g_identity_initialized.store(true, std::memory_order_release);
    }
  }
  return g_hardware_serial_number;
}

const char *firmware_version() {
  return system_firmware_version();
}

} // namespace lunaredu::lunaros
