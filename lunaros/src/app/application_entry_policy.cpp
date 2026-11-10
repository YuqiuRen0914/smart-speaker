#include "lunaredu_lunaros/app/application_entry_policy.h"

#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::app {

ApplicationEntryPolicyStore::ApplicationEntryPolicyStore(
    ApplicationEntryPolicyConfig config)
    : config_(config) {}

void ApplicationEntryPolicyStore::setup() {
  if (initialized_) {
    return;
  }
  policy_.boot_action = lunaredu::lunaros::settings_get_string(
      config_.settings_namespace, config_.settings_key,
      config_.default_boot_action);
  policy_.user_action = config_.user_action;
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
  if (!lunaredu::lunaros::settings_set_string(
          config_.settings_namespace, config_.settings_key, action)) {
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
  ++policy_.revision;
}

std::string ApplicationEntryPolicyStore::action_for_reason(
    const ApplicationEntryPolicySnapshot &policy,
    ApplicationEnterReason reason) const {
  return reason == ApplicationEnterReason::Boot ? policy.boot_action
                                                 : policy.user_action;
}

} // namespace lunaredu::app
