#pragma once
#include "lunaredu_pet_conversation.h"
namespace lunaredu::pet::conversation {
class SuspensionPolicy {
public:
  void set(SuspensionReason reason, bool active) {
    const auto bit = static_cast<uint8_t>(reason);
    reasons_ = active ? reasons_ | bit : reasons_ & ~bit;
  }
  bool suspended() const { return reasons_ != 0; }
  const char *detail() const {
    return reasons_ == 3 ? "audio_focus,cloud_maintenance" :
           reasons_ == 1 ? "audio_focus" :
           reasons_ == 2 ? "cloud_maintenance" : "";
  }
private:
  uint8_t reasons_ = 0;
};
}
