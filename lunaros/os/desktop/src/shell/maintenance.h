#pragma once
#include "desktop/shell/shell.h"
#include "app/package/application_data_ports.h"

namespace lunaredu::desktop {
// Consumes the confirmed stable App ID. Core owns package/data mutation;
// the desktop commits its projection only after that mutation succeeds.
struct HomeUninstallOperation {
  std::string id;
  lunaros::apps::ApplicationOperation receipt;
  bool request(DesktopShell &,const lunaros::apps::ApplicationDataPorts &,std::string &error);
  // True only on a terminal observation. Pending never edits desktop layout.
  bool poll(DesktopShell &,const lunaros::apps::ApplicationDataPorts &,std::string &error);
};
}
