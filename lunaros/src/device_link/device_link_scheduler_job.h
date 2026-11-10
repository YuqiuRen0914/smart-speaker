#pragma once

#include "lunaredu_lunaros/network/network_jobs.h"

namespace lunaredu::lunaros::device_link {

enum class DeviceLinkOperation : uint8_t {
  None,
  OpenClaimEnrollment,
  CancelPairing,
  Unbind,
  CreatePairing,
  Presence,
  FirmwareUpdate,
  Commands,
  PollPairing,
  StartDiscovery,
  Discovery,
};

struct DeviceLinkJobContext {
  DeviceLinkOperation operation = DeviceLinkOperation::None;
  NetworkJobHandle handle;
  uint32_t duration_ms = 0;
};

} // namespace lunaredu::lunaros::device_link
