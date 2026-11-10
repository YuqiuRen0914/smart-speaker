#pragma once
#include <string>
#include <memory>
#include "desktop/ui/wallpaper.h"
#include "lunaros_settings_bluetooth.h"

namespace lunaredu::settings {
// Screen-local presentation only; service credentials and operations stay with
// their owners. setup_password is the device provisioning AP password.
struct SystemPresentation {
  bool wifi_connected = false, platform_connected = false, platform_available = false;
  bool provisioning = false, updating = false;
  bool provisioning_available = false, provisioning_ble = false;
  unsigned setup_remaining_seconds = 0;
  std::string setup_phase, setup_error;
  bool bluetooth_available=false,bluetooth_enabled=false,bluetooth_connected=false;
  bool network_busy=false,network_failed=false;
  std::shared_ptr<const BluetoothPresentation> bluetooth;
  unsigned bluetooth_scroll=0;
  std::string wifi_ssid, local_ip, setup_ssid, setup_password, setup_ip, setup_url;
  enum class UpdateCheck { Idle, Checking, Current, Available, Failed };
  UpdateCheck update_check = UpdateCheck::Idle;
  bool platform_bound = false, pairing_pending = false;
  std::string pairing_code, pairing_url, pairing_error, account_name;
  std::shared_ptr<const desktop::DesktopImage> account_avatar;
  int pairing_seconds = 0;
  std::string available_version, update_error, operation_message;
  unsigned update_percent = 0, activity_phase = 0;
  bool update_progress_known = false, update_restarting = false;
  bool operator==(const SystemPresentation &) const;
};
}
