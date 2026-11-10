#include <cassert>
#include <iostream>
#include "esp_timer.h"
#include "freertos/queue.h"
#include "lunaredu_pet_service.h"
#include "runtime/conversation_state.h"
#include "lunaredu_lunaros/app/app_capability_json.h"

int main() {
  using namespace lunaredu;
  using presentation::Expression;
  std::string error;
  assert(!pet::post_touch_reaction("happy"));
  test_fail_queue = true;
  assert(!pet::setup_runtime());
  test_fail_queue = false;
  assert(pet::setup_runtime());
  assert(!pet::snapshot().active && pet::snapshot().expression == Expression::Idle);
  assert(!pet::post_touch_reaction("happy"));
  pet::update_environment({.boot_progress = 100});
  pet::runtime::update_conversation_phase(pet::PetConversationPhase::Ready);
  pet::set_runtime_active(true);

  // Repeated activation/setup must preserve pending accepted interactions.
  assert(pet::post_touch_reaction("happy"));
  pet::set_runtime_active(true);
  assert(pet::setup_runtime());
  pet::tick_runtime();
  assert(pet::snapshot().pet.mood == pet::PetMood::Happy);
  assert(pet::snapshot().transient_expression_active);
  pet::runtime::update_conversation_phase(pet::PetConversationPhase::Listening);
  assert(pet::snapshot().expression == Expression::Listening);
  assert(pet::snapshot().pet.mood == pet::PetMood::Happy);

  // Stopping publishes a coherent snapshot immediately, without another tick.
  assert(pet::post_touch_reaction("sad"));
  pet::tick_runtime();
  assert(pet::post_touch_reaction("angry"));
  pet::set_runtime_active(false);
  auto stopped = pet::snapshot();
  assert(!stopped.active && !stopped.transient_expression_active);
  assert(stopped.expression == Expression::Idle && stopped.theme_state == "idle");
  assert(stopped.conversation_phase == pet::PetConversationPhase::Disabled);
  assert(stopped.pet.mood == pet::PetMood::Sad && stopped.boot_complete);
  assert(!pet::post_touch_reaction("happy"));
  assert(!pet::post_interaction_event({pet::InteractionEventKind::Emotion,"angry",nullptr}));
  pet::set_runtime_active(false);
  pet::runtime::update_conversation_phase(pet::PetConversationPhase::Speaking);
  assert(pet::snapshot().conversation_phase == pet::PetConversationPhase::Disabled);
  pet::set_runtime_active(true);
  assert(pet::snapshot().conversation_phase == pet::PetConversationPhase::Speaking);
  pet::tick_runtime();
  assert(pet::snapshot().pet.mood == pet::PetMood::Sad); // Queued angry was cancelled.

  // No renderer settings implementation is linked. Exact emotion survives.
  assert(pet::post_touch_reaction("laughing"));
  pet::tick_runtime();
  assert(pet::snapshot().theme_state == "laughing");
  assert(pet::snapshot().pet.mood == pet::PetMood::Happy);
  pet::runtime::update_conversation_phase(pet::PetConversationPhase::Ready);
  test_time_us = static_cast<int64_t>((uint64_t{1} << 32) - 2600) * 1000;
  assert(pet::post_touch_reaction("laughing"));
  pet::tick_runtime();
  assert(pet::snapshot().transient_expression_active); // Deadline is exactly zero.
  test_time_us += 100000;
  pet::tick_runtime();
  assert(pet::snapshot().theme_state == "laughing");
  test_time_us += 2500000;
  pet::tick_runtime();
  assert(!pet::snapshot().transient_expression_active);
  assert(pet::snapshot().theme_state == "happy");

  JsonDocument args, result;
  args.to<JsonObject>();
  assert(app::call_json_capability("product","pet.state.get",1,
      args.as<JsonObjectConst>(),result.to<JsonObject>(),error));
  assert(!app::describe_json_capability("pet","pet.activity.start",result.to<JsonObject>(),error));
  assert(!app::describe_json_capability("pet","motion.activity.start",result.to<JsonObject>(),error));
  std::cout << "Pet lifecycle, exact emotion, internal phase and wraparound passed without providers or settings linked\n";
}
