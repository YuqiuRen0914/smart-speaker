#pragma once
#include "desktop/shell/shell.h"
#include "app/package/application_data_ports.h"
namespace lunaredu::settings {
inline constexpr char kApplicationDataKind = 0;
enum class DataPage { Applications, Confirm };
enum class DataAction : uint32_t { Apply=1200, Cancel, DeleteData, Refresh };
// Installed package management. Save editing belongs to applications.
struct ApplicationDataState : desktop::DesktopSurfaceState {
  ApplicationDataState() : desktop::DesktopSurfaceState(&kApplicationDataKind) {}
  lunaros::apps::ApplicationDataPorts ports;
  lunaros::apps::ApplicationOperation operation;
  lunaros::apps::ApplicationList applications;
  DataPage page=DataPage::Applications;
  size_t selected=0;
  unsigned action=0;
  uint64_t revision=0;
  bool failed=false,delete_data=false;
  std::string message,operation_id;
};
std::shared_ptr<const ApplicationDataState> application_data(const desktop::DesktopShell &);
void bind_application_data(desktop::DesktopShell &,lunaros::apps::ApplicationDataPorts);
bool open_application_data(desktop::DesktopShell &);
bool application_data_action(desktop::DesktopShell &,DataAction);
const desktop::DesktopSurfaceExtension &application_data_extension();
}
