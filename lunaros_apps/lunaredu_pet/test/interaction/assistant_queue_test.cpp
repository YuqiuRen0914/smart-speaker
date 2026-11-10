#include "interaction/voice/assistant_audio_pool.h"
#include "interaction/tools/assistant_mcp_queue.h"

#include <cassert>
#include <memory>
#include <string>

int main() {
  using namespace lunaredu::pet::conversation::internal;
  PendingMcpMessages messages;
  for (size_t round = 0; round < 100; ++round) {
    for (size_t n = 0; n < PendingMcpMessages::kCapacity; ++n)
      assert(messages.try_push(std::to_string(n)));
    assert(!messages.try_push("overflow"));
    for (size_t n = 0; n < PendingMcpMessages::kCapacity; ++n)
      assert(messages.pop() == std::to_string(n));
    assert(messages.empty() && messages.bytes() == 0);
  }
  assert(!messages.try_push(std::string(32769, 'x')));
  assert(messages.try_push(std::string(32768, 'a')));
  assert(messages.try_push(std::string(32768, 'b')));
  assert(!messages.try_push("x"));
  assert(!messages.can_accept(1, 32768));
  messages.clear();
  assert(messages.bytes() == 0 && messages.can_accept(2, 33792));

  DownlinkAudioPool pool;
  std::array<DownlinkAudioWorkItem *, DownlinkAudioPool::kCapacity> held{};
  for (size_t n = 0; n < held.size(); ++n) {
    std::vector<uint8_t> payload(160, static_cast<uint8_t>(n));
    const auto *data = payload.data();
    held[n] = pool.acquire(std::move(payload));
    assert(held[n] != nullptr && held[n]->frame.data() == data);
  }
  assert(pool.acquire({1, 2, 3}) == nullptr);
  for (size_t n = 0; n < held.size(); ++n) {
    assert(held[n]->frame[0] == n);
    DownlinkAudioWorkItem::release(held[n]);
    assert(held[n]->frame.capacity() == 0);
  }
  {
    std::unique_ptr<DownlinkAudioWorkItem, decltype(&DownlinkAudioWorkItem::release)>
        item(pool.acquire({42}), DownlinkAudioWorkItem::release);
    assert(item && item->frame[0] == 42);
  }
  // Early-return and reset paths must make every slot reusable.
  for (auto &item : held) { item = pool.acquire({9}); assert(item); }
  for (auto *item : held) DownlinkAudioWorkItem::release(item);
  return 0;
}
