#pragma once
#include "kernel/product_definition.h"
#include <ArduinoJson.h>
#include <span>
#include <string>
namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
struct FirmwareInstallationPolicy {
  const char *settings_namespace="fw_install";
  const char *endpoint_id="firmware_installation";
  std::span<const char *const> required_assets{};
  bool transaction_required=false;
  bool existing_assets_only=false;
};
// One physical device and one boot-lived installation owner. Products declare
// requirements; OTA owns ELF intent, durable plans, recovery and activation.
bool configure_firmware_installation(FirmwareInstallationPolicy,std::string &error);
bool firmware_installation_pending();
bool validate_firmware_installation_assets(std::string &error);
void configure_firmware_installation_recovery(bool enabled);
bool begin_firmware_installation(std::string &error);
bool resume_firmware_installation(std::string &error);
bool complete_firmware_installation(std::string &error);
void firmware_installation_accept_cloud_assets(JsonVariantConst);
void poll_firmware_installation();
ProductDeclarationNode firmware_installation_declaration(DeviceProjectionRegistry &);
}
