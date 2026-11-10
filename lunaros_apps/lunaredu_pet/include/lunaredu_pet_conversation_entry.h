#pragma once

#include <cstdint>
#include <string>

#include "lunaredu_lunaros/app/application.h"

namespace lunaredu::pet::conversation {

enum class EntryAction : uint8_t {
  ShowApplication,
  StartListening,
};

struct EntryPolicySnapshot {
  EntryAction boot_action = EntryAction::ShowApplication;
  EntryAction user_action = EntryAction::ShowApplication;
  uint32_t revision = 0;
};

void setup_entry_policy();
EntryPolicySnapshot entry_policy_snapshot();
bool set_boot_action(EntryAction action, std::string &error);
EntryAction action_for_reason(const EntryPolicySnapshot &policy,
                              lunaredu::app::ApplicationEnterReason reason);

} // namespace lunaredu::pet::conversation
