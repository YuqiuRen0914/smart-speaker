#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include <utility>

namespace lunaredu::pet::conversation::internal {

struct DownlinkAudioWorkItem {
  std::atomic<bool> occupied{false};
  std::vector<uint8_t> frame;
  uint32_t generation = 0;
  uint32_t sequence = 0;
  static void release(DownlinkAudioWorkItem *item) {
    if (item == nullptr) return;
    // Preserve the existing payload reclamation on playback/drop/reset. Only
    // the small queue envelope is pooled; transport payloads still allocate.
    std::vector<uint8_t>().swap(item->frame);
    item->occupied.store(false, std::memory_order_release);
  }
};

class DownlinkAudioPool {
public:
  static constexpr size_t kCapacity = 18; // queue + consumer + producer
  DownlinkAudioWorkItem *acquire(std::vector<uint8_t> &&frame) {
    for (auto &item : items_) {
      bool available = false;
      if (item.occupied.compare_exchange_strong(
              available, true, std::memory_order_acquire)) {
        item.frame = std::move(frame);
        return &item;
      }
    }
    return nullptr;
  }
private:
  std::array<DownlinkAudioWorkItem, kCapacity> items_{};
};

} // namespace lunaredu::pet::conversation::internal
