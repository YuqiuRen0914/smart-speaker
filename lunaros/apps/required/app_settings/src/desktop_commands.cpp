#include "lunaros_settings_desktop_commands.h"
#if defined(LUNAROS_SETTINGS_PROVISIONING_COMPONENT)
#include "lunaros_provisioning_activity.h"
#endif
#if defined(LUNAROS_SETTINGS_PLATFORM_COMPONENT)
#include "lunaros_platform_link_pairing_activity.h"
#endif
#include <mutex>

namespace lunaredu::settings {
bool apply_desktop_command(lunaros::ApplicationContext &device,UiCommand command,std::string &error,
    bool (*platform_command)(lunaros::ApplicationContext &,UiCommand,std::string &)) {
  if(command==UiCommand::StartProvisioning || command==UiCommand::StopProvisioning) {
#if defined(LUNAROS_SETTINGS_PROVISIONING_COMPONENT)
    static std::mutex mutex; static lunaros::ProvisioningActivityHandle activity;
    std::lock_guard<std::mutex> lock(mutex);
    return command==UiCommand::StartProvisioning ? lunaros::begin_provisioning_activity("settings",activity,error)
        : lunaros::cancel_provisioning_activity("settings",activity,error);
#else
    error="provisioning_unavailable";return false;
#endif
  }
  if(command==UiCommand::GeneratePairing) {
#if defined(LUNAROS_SETTINGS_PLATFORM_COMPONENT)
    static std::mutex mutex; static lunaros::PlatformLinkPairingActivityHandle activity;
    std::lock_guard<std::mutex> lock(mutex);
    return lunaros::begin_platform_link_pairing_activity("settings",activity,error,true);
#else
    error="platform_pairing_unavailable";return false;
#endif
  }
  if(command==UiCommand::BluetoothOn)return queue_network_control(device.runtime,NetworkControl::BluetoothOn,error);
  if(command==UiCommand::BluetoothOff)return queue_network_control(device.runtime,NetworkControl::BluetoothOff,error);
  if(command==UiCommand::BluetoothPair)return queue_bluetooth_control(BluetoothControl::Pair,error);
  if(command==UiCommand::BluetoothScanBle)return queue_bluetooth_control(BluetoothControl::ScanBle,error);
  if(command==UiCommand::BluetoothScanClassic)return queue_bluetooth_control(BluetoothControl::ScanClassic,error);
  if(command==UiCommand::BluetoothConfirmPair)return queue_bluetooth_control(BluetoothControl::ConfirmPair,error);
  if(command==UiCommand::BluetoothRejectPair)return queue_bluetooth_control(BluetoothControl::RejectPair,error);
  if(platform_command)return platform_command(device,command,error);
  error="unknown_settings_command"; return false;
}
}
