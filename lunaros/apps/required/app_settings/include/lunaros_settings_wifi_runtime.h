#pragma once
#include "lunaros_settings_wifi.h"

namespace lunaredu::lunaros {class KernelRuntime;}
namespace lunaredu::desktop {
enum class WifiLink : uint8_t { Down, Connecting, Up };
enum class WifiSubmission : uint8_t { Pending, Applied, ConnectedUnsaved, SaveFailed, ApplyFailed, Busy, Cancelled };
struct WifiObservation {
  WifiContent content;
  uint32_t scan_sequence=0,network_sequence=0,failure_started_ms=0;
  WifiLink link=WifiLink::Down;
  bool authentication_failed=false;
  char target_ssid[33]{};
};
struct WifiNetworkPort {
  void *context=nullptr;
  WifiObservation (*observe)(void *)=nullptr;
  bool (*scan)(void *)=nullptr;
  bool (*submit)(void *,const WifiAccessPoint &,const char *,bool forget,uint32_t generation)=nullptr;
  WifiSubmission (*submission)(void *,uint32_t generation)=nullptr;
  void (*abandon)(void *)=nullptr;
};
// One foreground session on the input owner. No new task, timer or blocking
// scan; the backend owns queued-operation lifetime beyond a UI exit.
class WifiRuntime {
public:
  explicit WifiRuntime(WifiNetworkPort port):port_(port){}
  bool update(DesktopShell &,uint32_t now_ms,bool visible=true);
private:
  WifiNetworkPort port_;
  uint32_t generation_=0,scan_sequence_=0,network_sequence_=0,started_=0,last_poll_=0;
  bool active_=false,connecting_=false,forgetting_=false;
};
// Register before starting the input owner; null restores unavailable UI.
void configure_desktop_wifi(lunaredu::lunaros::KernelRuntime *);
bool update_desktop_wifi(DesktopShell &,uint32_t now_ms,bool visible=true);
} // namespace lunaredu::desktop

namespace lunaredu::settings {
enum class NetworkControl { BluetoothOn, BluetoothOff };
bool queue_network_control(lunaros::KernelRuntime &,NetworkControl,std::string &error);
struct NetworkControlStatus {bool busy=false,failed=false;NetworkControl control=NetworkControl::BluetoothOn;};
NetworkControlStatus network_control_status();
}
