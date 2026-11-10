#pragma once
#include "lunaredu_pet_setup.h"
#include "app/lifecycle/app_capability.h"
namespace lunaredu::pet {
enum class SetupKind { Network, Account };
enum class SetupPhase { Preparing, Waiting, Succeeded, Failed, Cancelling, Cancelled };
struct SetupPresentation {
  SetupKind kind = SetupKind::Account;
  SetupPhase phase = SetupPhase::Failed;
  std::string code, url, ssid, password;
};
bool request_setup(SetupKind, std::string &);
bool cancel_setup(std::string &);
void close_setup_presentation();
bool setup_presentation(SetupPresentation &);
bool register_setup_capabilities(app::CapabilityRegistry &, std::string &);
}
