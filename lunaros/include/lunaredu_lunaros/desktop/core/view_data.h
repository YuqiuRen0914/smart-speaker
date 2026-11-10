#pragma once
#include <array>
#include <string>
#include "lunaredu_lunaros/desktop/core/ui_contract.h"
namespace lunaredu::desktop {

struct LauncherItem {
  std::string application_id;
  std::string title;
  std::string icon;
  uint32_t accent_rgb = 0x38BDF8;
  bool available = true;
};

struct FileViewEntry {
  std::string name;
  std::string path;
  std::string detail;
  FileKind kind = FileKind::Other;
};

struct LunarOSWifiAccessPoint {
  std::array<char, 33> ssid{};
  int rssi_dbm = 0;
  int quality = 0;
  bool secure = false;
  bool saved = false;
};

enum class LunarOSServerConnectionState : uint8_t {
  Disconnected = LUNAROS_SERVER_DISCONNECTED,
  Connected = LUNAROS_SERVER_CONNECTED,
  Error = LUNAROS_SERVER_ERROR,
};

enum class LunarOSHostConnectionState : uint8_t {
  Disconnected = LUNAROS_HOST_DISCONNECTED,
  Connected = LUNAROS_HOST_CONNECTED,
  Error = LUNAROS_HOST_ERROR,
  Lan = LUNAROS_HOST_LAN,
};

}
