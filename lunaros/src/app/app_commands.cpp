#include "lunaredu_lunaros/app/app_commands.h"

#include "lunaredu_lunaros/kernel/service_executor.h"

namespace lunaredu::app {
namespace {

AppController *g_controller = nullptr;

enum class ApplicationCommandOpcode : uint8_t {
  Status,
  Snapshot,
  Switch,
  Stop,
};

struct ApplicationCommand {
  ApplicationCommandOpcode opcode = ApplicationCommandOpcode::Status;
  std::string text;
  size_t index = 0;
  AppControllerStatusSnapshot *status = nullptr;
  ApplicationSnapshot *application = nullptr;
};

void execute_application_command(
    ApplicationCommand &command,
    lunaredu::lunaros::ServiceOperationResult &result) {
  if (g_controller == nullptr) {
    result.ok = false;
    result.error = "application_controller_unavailable";
    return;
  }
  switch (command.opcode) {
  case ApplicationCommandOpcode::Status:
    break;
  case ApplicationCommandOpcode::Snapshot:
    if (command.application == nullptr ||
        !g_controller->application_snapshot_into(command.index,
                                                 *command.application)) {
      result.ok = false;
      result.error = "application_index_out_of_range";
    }
    return;
  case ApplicationCommandOpcode::Switch:
    if (!g_controller->switch_to(command.text, result.error)) {
      result.ok = false;
    }
    break;
  case ApplicationCommandOpcode::Stop:
    g_controller->stop(command.text.empty() ? "remote_stop"
                                            : command.text.c_str());
    break;
  }
  if (command.status != nullptr) {
    g_controller->status_snapshot_into(*command.status);
  }
}

bool invoke(ApplicationCommand &command, std::string &error) {
  return lunaredu::lunaros::invoke_service_operation(
      lunaredu::lunaros::kMainServiceExecutor, execute_application_command,
      command, error);
}

} // namespace

bool bind_app_controller(AppController &controller, std::string &error) {
  error.clear();
  if (g_controller != nullptr && g_controller != &controller) {
    error = "application_controller_already_bound";
    return false;
  }
  g_controller = &controller;
  return true;
}

bool request_application_switch(const std::string &application_id,
                                AppControllerStatusSnapshot &snapshot,
                                std::string &error) {
  if (application_id.empty()) {
    error = "missing_application_id";
    return false;
  }
  ApplicationCommand command;
  command.opcode = ApplicationCommandOpcode::Switch;
  command.text = application_id;
  command.status = &snapshot;
  return invoke(command, error);
}

bool request_application_status(AppControllerStatusSnapshot &snapshot,
                                std::string &error) {
  ApplicationCommand command;
  command.status = &snapshot;
  return invoke(command, error);
}

bool request_application_snapshot(size_t index, ApplicationSnapshot &snapshot,
                                  std::string &error) {
  ApplicationCommand command;
  command.opcode = ApplicationCommandOpcode::Snapshot;
  command.index = index;
  command.application = &snapshot;
  return invoke(command, error);
}

bool request_application_stop(const std::string &reason,
                              AppControllerStatusSnapshot &snapshot,
                              std::string &error) {
  ApplicationCommand command;
  command.opcode = ApplicationCommandOpcode::Stop;
  command.text = reason;
  command.status = &snapshot;
  return invoke(command, error);
}

} // namespace lunaredu::app
