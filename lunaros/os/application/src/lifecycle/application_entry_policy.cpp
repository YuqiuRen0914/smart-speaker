#include "app/lifecycle/application_entry_policy.h"

namespace lunaredu::app {

ApplicationEntryPolicyStore::ApplicationEntryPolicyStore(
    ApplicationEntryPolicyConfig config)
    : config_(config) {}

void ApplicationEntryPolicyStore::setup() {
  if (initialized_) {
    return;
  }
  policy_.user_action = config_.user_action ? config_.user_action : "";
  std::string stored;
  const auto result = config_.read_setting
      ? config_.read_setting(config_.settings_namespace, config_.settings_key,
                             64, stored, policy_.read_error)
      : EntryPolicyReadResult::Missing;
  if (result == EntryPolicyReadResult::Failed) {
    policy_.boot_action = policy_.user_action;
    return; // Retry later; never treat an unreadable record as first boot.
  }
  policy_.read_error.clear();
  policy_.boot_action = result == EntryPolicyReadResult::Found
      ? stored : (config_.default_boot_action ? config_.default_boot_action : "");
  policy_.revision = 1;
  initialized_ = true;
}

ApplicationEntryPolicySnapshot ApplicationEntryPolicyStore::snapshot() {
  setup();
  return policy_;
}

bool ApplicationEntryPolicyStore::set_boot_action(const std::string &action,
                                                   std::string &error) {
  setup();
  if (!initialized_) { error = policy_.read_error; return false; }
  if (!config_.write_setting ||
      !config_.write_setting(config_.settings_namespace, config_.settings_key,
                             action)) {
    error = config_.write_error;
    return false;
  }
  if (policy_.boot_action != action) {
    policy_.boot_action = action;
    ++policy_.revision;
  }
  error.clear();
  return true;
}

void ApplicationEntryPolicyStore::note_related_change() {
  setup();
  if (initialized_) ++policy_.revision;
}

std::string ApplicationEntryPolicyStore::action_for_reason(
    const ApplicationEntryPolicySnapshot &policy,
    ApplicationEnterReason reason) const {
  return reason == ApplicationEnterReason::Boot ? policy.boot_action
                                                 : policy.user_action;
}

} // namespace lunaredu::app
