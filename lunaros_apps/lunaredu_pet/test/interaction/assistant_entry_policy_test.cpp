#include <iostream>
#include <string>
#include <unordered_map>

#include "lunaredu_pet_conversation_entry.h"

namespace {

int g_failures = 0;
std::unordered_map<std::string, std::string> g_settings;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__                              \
                << " check failed: " #condition << std::endl;                \
      ++g_failures;                                                            \
    }                                                                          \
  } while (false)

std::string setting_key(const char *name_space, const char *key) {
  return std::string(name_space != nullptr ? name_space : "") + ":" +
         (key != nullptr ? key : "");
}

} // namespace

namespace lunaredu::lunaros {

std::string settings_get_string(const char *name_space, const char *key,
                                const char *default_value) {
  const auto found = g_settings.find(setting_key(name_space, key));
  return found == g_settings.end()
             ? std::string(default_value != nullptr ? default_value : "")
             : found->second;
}

bool settings_set_string(const char *name_space, const char *key,
                         const std::string &value) {
  g_settings[setting_key(name_space, key)] = value;
  return true;
}

} // namespace lunaredu::lunaros

int main() {
  using lunaredu::pet::conversation::EntryAction;
  using lunaredu::app::ApplicationEnterReason;

  lunaredu::pet::conversation::setup_entry_policy();
  auto policy = lunaredu::pet::conversation::entry_policy_snapshot();
  CHECK(lunaredu::pet::conversation::action_for_reason(
            policy, ApplicationEnterReason::Boot) ==
        EntryAction::ShowApplication);
  CHECK(lunaredu::pet::conversation::action_for_reason(
            policy, ApplicationEnterReason::User) ==
        EntryAction::ShowApplication);

  std::string error;
  CHECK(lunaredu::pet::conversation::set_boot_action(
      EntryAction::StartListening, error));
  policy = lunaredu::pet::conversation::entry_policy_snapshot();
  CHECK(lunaredu::pet::conversation::action_for_reason(
            policy, ApplicationEnterReason::Boot) ==
        EntryAction::StartListening);
  CHECK(lunaredu::pet::conversation::action_for_reason(
            policy, ApplicationEnterReason::User) ==
        EntryAction::ShowApplication);

  if (g_failures != 0) {
    return 1;
  }
  std::cout << "4 assistant entry policy checks passed" << std::endl;
  return 0;
}
