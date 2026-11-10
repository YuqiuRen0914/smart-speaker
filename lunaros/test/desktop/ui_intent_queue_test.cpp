#include "lunaredu_lunaros/desktop/core/ui_intent_queue.h"

#include <array>
#include <cassert>
#include <string>

int main() {
  using namespace lunaredu::desktop;

  LunarOSUiIntentQueue queue;
  assert(!queue.enqueue(nullptr, "{}"));
  assert(!queue.enqueue("", "{}"));
  assert(queue.enqueue("system.home", nullptr));

  std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> output{};
  assert(queue.take(output) == 1);
  assert(std::string(output[0].type.data()) == "system.home");
  assert(std::string(output[0].payload.data()) == "{}");

  for (size_t index = 0; index < kLunarOSUiIntentQueueCapacity; ++index) {
    assert(queue.enqueue("app.open", "{\"applicationId\":\"pet\"}"));
  }
  assert(!queue.enqueue("app.open", "{}"));
  assert(queue.take(output) == kLunarOSUiIntentQueueCapacity);

  const std::string long_type(kLunarOSUiIntentTypeCapacity, 't');
  const std::string long_payload(kLunarOSUiIntentPayloadCapacity, 'p');
  assert(!queue.enqueue(long_type.c_str(), "{}"));
  assert(!queue.enqueue("pet.touch", long_payload.c_str()));

  assert(queue.enqueue("system.back", "{}"));
  queue.reset();
  assert(queue.take(output) == 0);

  std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> injected_storage{};
  LunarOSUiIntentQueue injected_queue(injected_storage);
  assert(injected_queue.enqueue("system.settings", "{}"));
  LunarOSUiIntent pending;
  assert(injected_queue.take_one(pending));
  assert(std::string(pending.type.data()) == "system.settings");
  assert(!injected_queue.take_one(pending));
  return 0;
}
