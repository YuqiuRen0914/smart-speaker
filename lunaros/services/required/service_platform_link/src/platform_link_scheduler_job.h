#pragma once

#include "lunaros_network_jobs.h"

namespace lunaredu::lunaros::platform_link {

enum class PlatformLinkOperation : uint8_t {
  None,
  PersistBinding,
  OpenClaimEnrollment,
  CancelPairing,
  Unbind,
  CreatePairing,
  Presence,
  FirmwareUpdate,
  FirmwareCheck,
  Commands,
  PollPairing,
  StartDiscovery,
  Discovery,
};

struct PlatformLinkJobContext {
  PlatformLinkOperation operation = PlatformLinkOperation::None;
  NetworkJobHandle handle;
  uint32_t duration_ms = 0;
  bool running = false;
};

} // namespace lunaredu::lunaros::platform_link
