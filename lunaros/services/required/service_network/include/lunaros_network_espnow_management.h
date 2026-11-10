#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
enum class EspNowManagementFamily : uint8_t { Provisioning=1, Diagnostics=2, Ota=3 };
inline constexpr size_t kEspNowManagementPayloadBytes=218;
struct EspNowManagementRequest {
  EspNowManagementFamily family;
  const uint8_t *data=nullptr;
  size_t size=0;
  uint32_t sequence=0;
};
// Called on Network's existing executor after official CCM authentication,
// per-session nonce, strictly increasing sequence and current authorization.
// Complete synchronously, or enqueue on the destination's existing owner.
// Output is a bounded protocol reply; never retain request/output pointers.
using EspNowManagementHandler=bool(*)(const EspNowManagementRequest &,uint8_t *,size_t &,std::string &);
// One immutable provider per family, with process lifetime. Identical rebinding
// is harmless. Providers enforce their own domain authorization as well.
bool espnow_bind_management_provider(EspNowManagementFamily,EspNowManagementHandler);
struct EspNowManagementWindow {
  std::array<uint8_t,6> peer{};
  const char *proof_of_possession=nullptr; // 8..64 bytes, copied before return.
  uint32_t duration_ms=60000; // 100..60000; native OTA-only may use 900000.
  uint8_t families=0; // Bit (1 << family), explicit least-privilege selection.
  // Trusted native caller owns this gate and its context until stop succeeds.
  // It must check the current physical/management authorization, not merely
  // the peer MAC. Rebinding/revocation must make it return false immediately.
  bool (*authorized)(void *)=nullptr;
  void *context=nullptr;
  // Optional Network-owner notification after all session/crypto/temporary
  // peer resources retire. May wipe native authorization context; no new work.
  void (*retired)(void *)=nullptr;
};
struct EspNowManagementStatus {
  bool active=false,authenticated=false,retiring=false;
  uint64_t expires_at_ms=0; // Monotonic, never a wall-clock timestamp.
  uint32_t generation=0,remaining_ms=0,accepted=0,rejected=0;
  uint8_t families=0;
  std::string last_result;
};
// Explicit native admission only. No boot auto-discovery, wildcard peer,
// arbitrary command shell, private key persistence or extra radio/task owner.
bool espnow_open_management(KernelRuntime &,const EspNowManagementWindow &,std::string &);
bool espnow_close_management(KernelRuntime &,std::string &);
EspNowManagementStatus espnow_management_status();
}
