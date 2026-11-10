#pragma once
#include <cstdint>

namespace lunaredu::lunaros {
// Installed through NetworkPolicy before Network starts. The Network service
// owns the STA radio and transport lifetime, including on radio-only products.
struct EspNowPolicy {
  bool enabled = false;
  uint8_t channel = 1;
  // ESP-IDF uses quarter-dBm units. Zero preserves the board/SDK default.
  int8_t max_tx_power_quarter_dbm = 0;
};
}
