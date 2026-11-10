#include "app/lifecycle/app_controller_view.h"
#include "desktop/application/downloaded_host.h"
#include <cstring>

namespace lunaredu::lunaros::apps {
bool DownloadedHost::declare(app::AppController &controller, const char *return_id,
                             DownloadedHostPorts ports, std::string &error) {
  if (controller_ || !return_id || !return_id[0] ||
      !std::strcmp(return_id, kDownloadedHostId) || !ports.open || !ports.close) {
    error = "invalid_downloaded_host"; return false;
  }
  ports_ = ports; return_id_ = return_id;
  static constexpr ResourceRequirement display[] = {{"display.main", ResourceAccess::Exclusive}};
  app::ForegroundDefinition definition;
  definition.id = kDownloadedHostId;
  definition.preparation = ports.preparation;
  definition.manifest.title = "下载应用运行器";
  definition.manifest.kind = app::ForegroundKind::Mode;
  if (ports.service_id) {
    service_=app::required_service(ports.service_id);
    definition.manifest.services=&service_;
    definition.manifest.service_count=1;
  }
  definition.manifest.resources = display;
  definition.manifest.resource_count = std::size(display);
  definition.context = this;
  definition.prepare = [](void *p, std::string &failure) {
    auto &host = *static_cast<DownloadedHost *>(p);
    if (host.selected_id_.empty()) { failure = "downloaded_application_not_selected"; return false; }
    return !host.ports_.prepare || host.ports_.prepare(host.ports_.context, kDownloadedHostId, failure);
  };
  definition.start = [](void *p, lunaredu::app::ApplicationEnterReason, std::string &failure) {
    auto &host = *static_cast<DownloadedHost *>(p);
    const auto opened = host.ports_.open(host.ports_.context, host.selected_id_, failure);
    if(opened != app::ApplicationStartResult::Started)return opened;
    const bool entered = !host.ports_.entered || host.ports_.entered(host.ports_.context, kDownloadedHostId, failure);
    if (entered) host.retiring_ = false;
    return entered ? app::ApplicationStartResult::Started : app::ApplicationStartResult::Failed;
  };
  definition.stop_checked = [](void *p, const char *, std::string &failure) {
    auto &host = *static_cast<DownloadedHost *>(p);
    host.retiring_ = true;
    if (!host.ports_.close(host.ports_.context, failure)) return false;
    return !host.ports_.left || host.ports_.left(host.ports_.context, kDownloadedHostId, failure);
  };
  if (!controller.register_foreground(definition, error)) return false;
  controller_ = &controller;
  return true;
}
bool DownloadedHost::open(const std::string &id, std::string &error) {
  if (!controller_ || id.empty()) { error = "invalid_downloaded_application"; return false; }
  const auto status = lunaredu::app::AppControllerView{controller_}.progress();
  if (status.transition_phase != app::AppTransitionPhase::Idle) {
    error = "downloaded_transition_pending"; return false;
  }
  const auto *active = lunaredu::app::AppControllerView{controller_}.active_application_id();
  if (active && !std::strcmp(active, kDownloadedHostId)) {
    if (id == selected_id_ && !retiring_ && status.state == app::AppControllerState::Running) {
      error.clear(); return true;
    }
    // Never change package identity while teardown or lease retirement is pending.
    if (!controller_->switch_to(return_id_, error)) return false;
  }
  selected_id_ = id;
  return controller_->switch_to(kDownloadedHostId, error);
}
} // namespace lunaredu::lunaros::apps
