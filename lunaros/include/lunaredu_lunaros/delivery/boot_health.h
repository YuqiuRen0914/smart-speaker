#pragma once
#include <cstdint>
namespace lunaredu::lunaros {
class BootHealthGate {
public:
  explicit BootHealthGate(uint32_t healthy_period_ms = 3000)
      : period_(healthy_period_ms) {}
  bool ready(uint32_t now, bool pending, bool healthy) {
    if (!pending || !healthy) {
      tracking_ = false;
      return false;
    }
    if (!tracking_) {
      since_ = now;
      tracking_ = true;
    }
    return static_cast<uint32_t>(now - since_) >= period_;
  }

private:
  uint32_t period_, since_ = 0;
  bool tracking_ = false;
};
// System health plus product-specific hardware/executor health. Call on one
// owner task; update service retains responsibility for timeout and rollback.
void poll_boot_health(BootHealthGate &gate, bool application_ready);
} // namespace lunaredu::lunaros
