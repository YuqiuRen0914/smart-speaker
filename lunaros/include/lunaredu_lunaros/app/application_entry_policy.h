#pragma once

#include <cstdint>
#include <string>

#include "lunaredu_lunaros/app/application.h"

namespace lunaredu::app {

template <typename Action>
const Action &entry_action_for_reason(const Action &boot_action,
                                      const Action &user_action,
                                      ApplicationEnterReason reason) {
  return reason == ApplicationEnterReason::Boot ? boot_action : user_action;
}

struct ApplicationEntryPolicyConfig {
  const char *settings_namespace = nullptr;
  const char *settings_key = nullptr;
  const char *default_boot_action = nullptr;
  const char *user_action = nullptr;
  const char *write_error = "entry_policy_write_failed";
};

struct ApplicationEntryPolicySnapshot {
  std::string boot_action;
  std::string user_action;
  uint32_t revision = 0;
};

// Reusable persistence and enter-reason selection mechanism. Products retain
// ownership of their action vocabulary and validation rules.
class ApplicationEntryPolicyStore {
public:
  explicit ApplicationEntryPolicyStore(ApplicationEntryPolicyConfig config);

  void setup();
  ApplicationEntryPolicySnapshot snapshot();
  bool set_boot_action(const std::string &action, std::string &error);
  void note_related_change();
  std::string action_for_reason(const ApplicationEntryPolicySnapshot &policy,
                                ApplicationEnterReason reason) const;

private:
  ApplicationEntryPolicyConfig config_;
  ApplicationEntryPolicySnapshot policy_;
  bool initialized_ = false;
};

} // namespace lunaredu::app
