#pragma once

#include <array>
#include <cstddef>

namespace lunaredu::desktop {

inline constexpr size_t kLunarOSUiIntentQueueCapacity = 8;
inline constexpr size_t kLunarOSUiIntentTypeCapacity = 64;
inline constexpr size_t kLunarOSUiIntentPayloadCapacity = 192;

struct LunarOSUiIntent {
  std::array<char, kLunarOSUiIntentTypeCapacity> type{};
  std::array<char, kLunarOSUiIntentPayloadCapacity> payload{};
};

// Fixed-capacity ingress shared by local LVGL and remote Presentation
// controls. Producers only copy an envelope; the owning product executor
// drains and performs business work later.
class LunarOSUiIntentQueue {
public:
#ifdef ESP_PLATFORM
  LunarOSUiIntentQueue() = delete;
#else
  LunarOSUiIntentQueue();
#endif
  explicit LunarOSUiIntentQueue(
      std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> &storage);
  LunarOSUiIntentQueue(const LunarOSUiIntentQueue &) = delete;
  LunarOSUiIntentQueue &operator=(const LunarOSUiIntentQueue &) = delete;

  void reset();
  bool enqueue(const char *type, const char *payload);
  bool take_one(LunarOSUiIntent &output);
  size_t take(
      std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> &output);

private:
  void lock();
  void unlock();

#ifndef ESP_PLATFORM
  std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> owned_intents_{};
#endif
  LunarOSUiIntent *intents_ = nullptr;
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
};

} // namespace lunaredu::desktop
