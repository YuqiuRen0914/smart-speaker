#pragma once
#include "lunaros_platform_link.h"
namespace lunaredu::lunaros {
struct PlatformLinkPairingActivityHandle { uint64_t generation = 0; };
enum class PlatformLinkPairingActivityPhase { Preparing, Waiting, Succeeded, Failed, Cancelling, Cancelled };
struct PlatformLinkPairingActivitySnapshot {
  PlatformLinkPairingActivityHandle activity;
  PlatformLinkPairingActivityPhase phase = PlatformLinkPairingActivityPhase::Failed;
  PlatformLinkPairingPresentation pairing;
  uint32_t remaining_ms = 0;
  std::string error;
};
// Same caller/handle retries are idempotent; refresh rotates only an owned
// session. Cancellation covers queued creation and waits for retirement.
bool begin_platform_link_pairing_activity(const char *caller, PlatformLinkPairingActivityHandle &, std::string &, bool refresh = false);
bool read_platform_link_pairing_activity(const char *caller, PlatformLinkPairingActivityHandle, PlatformLinkPairingActivitySnapshot &, std::string &);
bool cancel_platform_link_pairing_activity(const char *caller, PlatformLinkPairingActivityHandle, std::string &);
}
