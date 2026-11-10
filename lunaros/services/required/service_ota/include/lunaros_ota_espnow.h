#pragma once
#include <cstdint>
#include <string>
#include <array>
namespace lunaredu::lunaros {
class KernelRuntime;
struct OtaEspNowCapabilities {
  bool staged_receive=false,send=false,official_packets=false;
  uint32_t maximum_image_bytes=0;
  uint16_t official_packet_bytes=0,official_bitmap_bytes=0;
};
// Compile-time support only: does not grant a session or imply free Storage.
OtaEspNowCapabilities ota_service_espnow_capabilities();
enum class OtaEspNowTransferPhase : uint8_t {
  Idle, Preparing, Receiving, Writing, Verifying, Ready, Installing, Completed, Failed, Retiring
};
struct OtaEspNowTransferStatus {
  uint32_t id=0,generation=0,expected_bytes=0,received_bytes=0,persisted_bytes=0;
  OtaEspNowTransferPhase phase=OtaEspNowTransferPhase::Idle;
  bool job_active=false;
  std::string last_error;
};
// Read-only native observation. Admission remains the exact-peer OTA-only
// management grant, plus the cached official package's full hash and size.
OtaEspNowTransferStatus ota_service_espnow_transfer_status();
enum class OtaEspNowSendPhase : uint8_t {
  Idle, Handshake, Begin, WaitingRemote, Reading, Sending, Verify, Install,
  InstallRequested, Failed, Retiring, QueryProgress
};
// Both modes use the exact-peer authenticated LunarOS management envelope.
// OfficialPackets reuses esp-now's DATA packet and paged STATUS bitmap ABI;
// it is not the upstream responder's unprotected Flash/reboot workflow.
enum class OtaEspNowWireMode : uint8_t { LunarEnvelope, OfficialPackets };
struct OtaEspNowSendRequest {
  std::array<uint8_t,6> peer{};
  const char *proof_of_possession=nullptr; // Copied by Network before return.
  const char *source_path=nullptr;
  const char *package_sha256=nullptr;
  uint32_t package_size=0,duration_ms=900000;
  bool (*authorized)(void *)=nullptr;
  void *context=nullptr; // Must live until sender status is no longer active.
  OtaEspNowWireMode wire_mode=OtaEspNowWireMode::LunarEnvelope;
};
struct OtaEspNowSendStatus {
  bool active=false;
  OtaEspNowSendPhase phase=OtaEspNowSendPhase::Idle;
  uint32_t generation=0,transfer_id=0,expected_bytes=0,persisted_bytes=0;
  std::string last_error;
  OtaEspNowWireMode wire_mode=OtaEspNowWireMode::LunarEnvelope;
};
// Explicit native maintenance use. The remote must already have authorized
// this exact peer and cached this exact official release. No discovery, URL,
// role override or automatic write replay. InstallRequested means the remote
// accepted the intent, never that it booted/confirmed the replacement image.
bool ota_service_start_espnow_send(KernelRuntime &,const OtaEspNowSendRequest &,std::string &error);
bool ota_service_cancel_espnow_send(std::string &error);
OtaEspNowSendStatus ota_service_espnow_send_status();
}
