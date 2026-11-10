#include "lunaredu_lunaros/device_web/portal.h"
namespace lunaredu::lunaros {
bool configure_device_web(const DeviceWebConfig &, std::string &error) {
  error = "device_web_disabled";
  return false;
}
}
