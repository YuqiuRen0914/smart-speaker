#include "lunaredu_pet_conversation_entry.h"

#include "lunaredu_lunaros/app/application_entry_policy.h"

namespace lunaredu::pet::conversation {
namespace {

constexpr const char *kSettingsNamespace = "pet_xiaozhi";
constexpr const char *kBootActionKey = "boot_action";
constexpr const char *kShowAction = "show";
constexpr const char *kListenAction = "listen";

lunaredu::app::ApplicationEntryPolicyStore g_store({
    kSettingsNamespace,
    kBootActionKey,
    kShowAction,
    kShowAction,
    "assistant_boot_action_write_failed",
});

} // namespace

void setup_entry_policy() {
  g_store.setup();
}

EntryPolicySnapshot entry_policy_snapshot() {
  const auto stored = g_store.snapshot();
  EntryPolicySnapshot policy;
  policy.boot_action = stored.boot_action == kListenAction
                           ? EntryAction::StartListening
                           : EntryAction::ShowApplication;
  policy.user_action = EntryAction::ShowApplication;
  policy.revision = stored.revision;
  return policy;
}

bool set_boot_action(EntryAction action, std::string &error) {
  const char *stored =
      action == EntryAction::StartListening ? kListenAction : kShowAction;
  return g_store.set_boot_action(stored, error);
}

EntryAction action_for_reason(const EntryPolicySnapshot &policy,
                              lunaredu::app::ApplicationEnterReason reason) {
  return lunaredu::app::entry_action_for_reason(
      policy.boot_action, policy.user_action, reason);
}

} // namespace lunaredu::pet::conversation
