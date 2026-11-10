#include "lunaredu_lunaros/persistence/identity.h"

#include <cstdio>
#include <string>

#include "kernel/system_primitives.h"

namespace {

std::string g_hardware_serial_number;

std::string build_hardware_serial_number() {
  uint8_t mac[6] = {0};
  if (!lunaredu::lunaros::internal::system_read_factory_mac(mac, sizeof(mac))) {
    return std::string("LUNAR-000000000000");
  }

  uint64_t chip_id = 0;
  for (uint8_t byte : mac) {
    chip_id = (chip_id << 8) | byte;
  }

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "LUNAR-%04X%08lX",
                static_cast<unsigned>(static_cast<uint16_t>(chip_id >> 32)),
                static_cast<unsigned long>(chip_id & 0xFFFFFFFFULL));
  return std::string(buffer);
}

} // 匿名命名空间

namespace lunaredu::lunaros {

const char *hardware_serial_number() {
  if (g_hardware_serial_number.empty()) {
    g_hardware_serial_number = build_hardware_serial_number();
  }
  return g_hardware_serial_number.c_str();
}

const char *firmware_version() {
  return internal::system_firmware_version();
}

} // namespace lunaredu::lunaros
