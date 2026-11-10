#pragma once
#include "lunaros_network_espnow_management.h"

namespace lunaredu::lunaros {
inline constexpr size_t kEspNowManagementBatchPeers=8;
struct EspNowManagementClientPeer {
  std::array<uint8_t,6> mac{};
  const char *proof_of_possession=nullptr; // Copied, 8..64 bytes.
};
enum class EspNowManagementClientResult : uint8_t {
  Pending, Succeeded, Rejected, TimedOut, TransportFailed, Cancelled
};
struct EspNowManagementClientStatus {
  bool active=false,authenticated=false,request_pending=false,retiring=false;
  uint32_t generation=0,sequence=0,remaining_ms=0;
  uint64_t expires_at_ms=0;
  uint8_t peer_index=0,peer_count=0;
  std::array<EspNowManagementClientResult,kEspNowManagementBatchPeers> results{};
  std::string last_result;
};
struct EspNowManagementClientOptions {
  uint32_t duration_ms=60000; // Whole operation, 100..900000 ms.
  uint32_t response_timeout_ms=3000; // 100..10000, no automatic write replay.
  uint8_t families=0;
  bool (*authorized)(void *)=nullptr;
  void *context=nullptr;
  // Network-owner callbacks, bounded and nonblocking. Do not reenter client
  // control or retain borrowed payload bytes. Native owner polls status to
  // schedule its next request. retired fires only after physical retirement.
  void (*response)(void *,uint8_t,EspNowManagementFamily,uint32_t,const uint8_t *,size_t)=nullptr;
  void (*retired)(void *)=nullptr;
  // Optional profile result decoder; default is the LEM1 payload's leading
  // success byte. Runs under the same nonblocking owner callback contract.
  // Used by native packet formats whose first byte is a message type.
  bool (*response_accepted)(void *,EspNowManagementFamily,const uint8_t *,size_t)=nullptr;
};
// One native session OR a sequential batch with one copied command per peer.
// Both share the responder's physical radio/crypto owner; no wildcard scan,
// group keys, upstream task, persisted PoP, or second Network executor.
bool espnow_open_management_client(KernelRuntime &,const EspNowManagementClientPeer &,
    const EspNowManagementClientOptions &,std::string &);
bool espnow_start_management_batch(KernelRuntime &,const EspNowManagementClientPeer *,size_t,
    const EspNowManagementClientOptions &,EspNowManagementFamily,const uint8_t *,size_t,std::string &);
bool espnow_management_client_request(KernelRuntime &,EspNowManagementFamily,
    const uint8_t *,size_t,std::string &);
bool espnow_close_management_client(KernelRuntime &,std::string &);
EspNowManagementClientStatus espnow_management_client_status();
}
