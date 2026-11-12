#include "interaction/conversation/assistant_turn_policy.h"

#include <cassert>
#include <cstdint>

using namespace lunaredu::pet::conversation::internal;

int main() {
  assert(single_turn_mode("manual"));
  assert(single_turn_mode("auto"));
  assert(!single_turn_mode("realtime"));
  assert(!single_turn_mode(nullptr));

  assert(!should_finish_single_turn_after_silence(
      "manual", false, 500, 1200, 300, 1000));
  assert(!should_finish_single_turn_after_silence(
      "manual", true, 299, 1200, 300, 1000));
  assert(!should_finish_single_turn_after_silence(
      "manual", true, 300, 999, 300, 1000));
  assert(should_finish_single_turn_after_silence(
      "manual", true, 300, 1000, 300, 1000));
  assert(should_finish_single_turn_after_silence(
      "auto", true, 300, 1000, 300, 1000));
  assert(!should_finish_single_turn_after_silence(
      "realtime", true, 300, 1000, 300, 1000));

  assert(!should_timeout_empty_single_turn(
      "manual", true, 1000, true, 20000, 15000));
  assert(!should_timeout_empty_single_turn(
      "manual", true, 1000, false, 15999, 15000));
  assert(should_timeout_empty_single_turn(
      "manual", true, 1000, false, 16000, 15000));
  assert(should_timeout_empty_single_turn(
      "auto", true, 1000, false, 16000, 15000));
  assert(!should_timeout_empty_single_turn(
      "realtime", true, 1000, false, 16000, 15000));
  assert(!should_timeout_empty_single_turn(
      "manual", false, 1000, false, 16000, 15000));
  assert(!should_timeout_empty_single_turn(
      "manual", true, 0, false, 16000, 15000));
  assert(!should_timeout_empty_single_turn(
      "manual", true, 1000, false, 999, 15000));

  assert(!should_finish_single_turn_at_limit("manual", true, 1000, 30999, 30000));
  assert(should_finish_single_turn_at_limit("manual", true, 1000, 31000, 30000));
  assert(should_finish_single_turn_at_limit("auto", true, 1000, 31000, 30000));
  assert(!should_finish_single_turn_at_limit("realtime", true, 1000, 31000, 30000));
  assert(!should_finish_single_turn_at_limit("manual", false, 1000, 31000, 30000));
  assert(!should_finish_single_turn_at_limit("manual", true, 0, 31000, 30000));
  assert(!should_finish_single_turn_at_limit("manual", true, 1000, 999, 30000));

  assert(std::strcmp(single_turn_stop_reason("manual"), "manual_finish") == 0);
  assert(std::strcmp(single_turn_stop_reason("auto"), "auto_stop") == 0);
  assert(!should_resume_after_response("manual", true));
  assert(should_resume_after_response("auto", true));
  assert(!should_resume_after_response("auto", false));
}
