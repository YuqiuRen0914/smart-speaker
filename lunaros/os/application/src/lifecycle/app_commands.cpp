#include "app/execution/application_context.h"
#include "kernel/runtime.h"
#include "app/lifecycle/app_commands.h"
#include "app/lifecycle/application_launch.h"
#include <cstring>

#include "kernel/service_executor.h"

namespace lunaredu::app {
namespace {
enum class LaunchCommandKind { Submit, Read, Cancel };
struct LaunchCommand {
  lunaros::ApplicationContext *device;
  const char *source, *id;
  ApplicationLaunch *token;
  ApplicationLaunchSnapshot *snapshot;
  LaunchCommandKind kind;
};
bool dispatch_launch(void *p, std::string &error) {
  auto &c=*static_cast<LaunchCommand *>(p);
  auto *controller=c.device->application();
  if(!controller){error="application_controller_missing";return false;}
  switch(c.kind) {
    case LaunchCommandKind::Submit:return controller->enqueue_launch(c.source,c.id,*c.token,error);
    case LaunchCommandKind::Read:return controller->launch_snapshot(c.source,*c.token,*c.snapshot,error);
    case LaunchCommandKind::Cancel:return controller->cancel_launch(c.source,*c.token,error);
  }
  return false;
}
void execute_launch(void *p,lunaros::ServiceOperationResult &result){result.ok=dispatch_launch(p,result.error);}
}
bool request_application_launch(lunaros::ApplicationContext &device,const char *source,
    const char *id,ApplicationLaunch &token,std::string &error) {
  LaunchCommand c{&device,source,id,&token,nullptr,LaunchCommandKind::Submit};
  return device.runtime.invoke_executor(lunaros::kMainServiceExecutor,execute_launch,&c,error);
}
bool application_launch_snapshot(lunaros::ApplicationContext &device,const char *source,
    ApplicationLaunch token,ApplicationLaunchSnapshot &snapshot,std::string &error) {
  LaunchCommand c{&device,source,nullptr,&token,&snapshot,LaunchCommandKind::Read};
  return device.runtime.invoke_executor(lunaros::kMainServiceExecutor,execute_launch,&c,error);
}
bool cancel_application_launch(lunaros::ApplicationContext &device,const char *source,
    ApplicationLaunch token,std::string &error) {
  LaunchCommand c{&device,source,nullptr,&token,nullptr,LaunchCommandKind::Cancel};
  return device.runtime.invoke_executor(lunaros::kMainServiceExecutor,execute_launch,&c,error);
}
bool declare_application_launch_capability(CapabilityRegistry &catalog,lunaros::ApplicationContext &device,std::string &error){
  static constexpr const char *callers[]={"product","assistant","agent","pet","micropython"};
  CapabilityDefinition definition{"application.launch","lunaros",1,sizeof(ApplicationLaunchRequest),sizeof(ApplicationLaunchResponse),
      [](void *p,const CapabilityCall &call,std::string &e){
        auto &device=*static_cast<lunaros::ApplicationContext *>(p);
        const auto &request=*static_cast<const ApplicationLaunchRequest *>(call.request);
        auto &response=*static_cast<ApplicationLaunchResponse *>(call.response);response.token=request.token;
        if(request.action!=ApplicationLaunchAction::Submit&&request.action!=ApplicationLaunchAction::Read&&request.action!=ApplicationLaunchAction::Cancel){e="invalid_application_launch_action";return false;}
        if(request.action==ApplicationLaunchAction::Submit){
          if(!std::memchr(request.application_id,0,sizeof(request.application_id))){e="invalid_application_launch";return false;}
          if(!request_application_launch(device,call.caller,request.application_id,response.token,e))return false;
        }
        if(request.action==ApplicationLaunchAction::Cancel&&!cancel_application_launch(device,call.caller,response.token,e))return false;
        return application_launch_snapshot(device,call.caller,response.token,response.snapshot,e);
      },nullptr,&device,callers,std::size(callers)};
  return catalog.register_capability(definition,error);
}
namespace {

enum class ApplicationCommandOpcode : uint8_t {
  Status,
  Snapshot,
  Switch,
  Stop,
};

struct ApplicationCommand {
  lunaredu::lunaros::ApplicationContext *device = nullptr;
  ApplicationCommandOpcode opcode = ApplicationCommandOpcode::Status;
  std::string text;
  size_t index = 0;
  AppControllerStatusSnapshot *status = nullptr;
  ApplicationSnapshot *application = nullptr;
};

void execute_application_command(
    ApplicationCommand &command,
    lunaredu::lunaros::ServiceOperationResult &result) {
  if (command.device == nullptr) {
    result.ok = false;
    result.error = "service_applications_missing";
    return;
  }
  auto *controller = command.device->application();
  if (controller == nullptr) {
    result.ok = false;
    result.error = "application_controller_unavailable";
    return;
  }
  switch (command.opcode) {
  case ApplicationCommandOpcode::Status:
    break;
  case ApplicationCommandOpcode::Snapshot:
    if (command.application == nullptr ||
        !controller->application_snapshot_into(command.index,
                                                 *command.application)) {
      result.ok = false;
      result.error = "application_index_out_of_range";
    }
    return;
  case ApplicationCommandOpcode::Switch:
    if (!controller->switch_to(command.text, result.error)) {
      result.ok = false;
    }
    break;
  case ApplicationCommandOpcode::Stop:
    result.ok = controller->stop(command.text.empty() ? "remote_stop"
                                            : command.text.c_str());
    if (!result.ok) {
      AppControllerStatusSnapshot status;
      controller->status_snapshot_into(status);
      result.error = status.last_error;
    }
    break;
  }
  if (command.status != nullptr) {
    controller->status_snapshot_into(*command.status);
  }
}

bool invoke(lunaredu::lunaros::ApplicationContext &device,
            ApplicationCommand &command, std::string &error) {
  return device.runtime.invoke_executor(
      lunaredu::lunaros::kMainServiceExecutor,
      [](void *opaque, lunaredu::lunaros::ServiceOperationResult &result) {
        execute_application_command(*static_cast<ApplicationCommand *>(opaque),
                                     result);
      },
      &command, error);
}

} // namespace

bool request_application_switch(lunaredu::lunaros::ApplicationContext &device,
                                const std::string &application_id,
                                AppControllerStatusSnapshot &snapshot,
                                std::string &error) {
  if (application_id.empty()) {
    error = "missing_application_id";
    return false;
  }
  ApplicationCommand command;
  command.device = &device;
  command.opcode = ApplicationCommandOpcode::Switch;
  command.text = application_id;
  command.status = &snapshot;
  return invoke(device, command, error);
}

bool request_application_status(lunaredu::lunaros::ApplicationContext &device,
                                AppControllerStatusSnapshot &snapshot,
                                std::string &error) {
  ApplicationCommand command;
  command.device = &device;
  command.status = &snapshot;
  return invoke(device, command, error);
}

bool request_application_snapshot(lunaredu::lunaros::ApplicationContext &device,
                                  size_t index, ApplicationSnapshot &snapshot,
                                  std::string &error) {
  ApplicationCommand command;
  command.device = &device;
  command.opcode = ApplicationCommandOpcode::Snapshot;
  command.index = index;
  command.application = &snapshot;
  return invoke(device, command, error);
}

bool request_application_stop(lunaredu::lunaros::ApplicationContext &device,
                              const std::string &reason,
                              AppControllerStatusSnapshot &snapshot,
                              std::string &error) {
  ApplicationCommand command;
  command.device = &device;
  command.opcode = ApplicationCommandOpcode::Stop;
  command.text = reason;
  command.status = &snapshot;
  return invoke(device, command, error);
}

} // namespace lunaredu::app
