#pragma once

#include <cstdint>

namespace lunaredu::lunaros::internal {

enum class RadioChannelDecision { Unavailable, Busy, Keep, Conflict, Retune };

constexpr RadioChannelDecision decide_radio_channel(
    bool started, bool scanning, bool connecting, bool associated,
    bool softap_active, uint8_t current, uint8_t requested) {
  if (!started || requested == 0 || requested > 14) {
    return RadioChannelDecision::Unavailable;
  }
  if (scanning || connecting) return RadioChannelDecision::Busy;
  if (current == requested) return RadioChannelDecision::Keep;
  if (associated || softap_active) return RadioChannelDecision::Conflict;
  return RadioChannelDecision::Retune;
}

} // namespace lunaredu::lunaros::internal
