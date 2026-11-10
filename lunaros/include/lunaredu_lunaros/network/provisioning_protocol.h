#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lunaredu::lunaros {

constexpr uint8_t kProvisioningProtocolVersion = 1;
constexpr size_t kProvisioningProtocolMaxPayloadSize = 2048;

enum class ProvisioningFrameType : uint8_t {
  GetInfo = 0x01,
  OpenSession = 0x02,
  ConfirmSession = 0x03,
  SetWifi = 0x04,
  GetStatus = 0x05,
  CloseSession = 0x06,
  Error = 0x7f,
};

constexpr uint8_t kProvisioningFrameResponseFlag = 0x80;

struct ProvisioningFrame {
  uint8_t version = kProvisioningProtocolVersion;
  uint8_t type = static_cast<uint8_t>(ProvisioningFrameType::GetInfo);
  uint32_t request_id = 0;
  std::string payload;
};

enum class ProvisioningFrameDecodeStatus : uint8_t {
  NeedMore,
  FrameReady,
  InvalidMagic,
  UnsupportedVersion,
  PayloadTooLarge,
  InvalidCrc,
  InvalidFrame,
};

bool encode_provisioning_frame(const ProvisioningFrame &frame,
                               std::vector<uint8_t> &encoded);

class ProvisioningFrameDecoder {
 public:
  ProvisioningFrameDecodeStatus append(const uint8_t *data, size_t length,
                                        std::vector<ProvisioningFrame> &frames);
  void reset();

 private:
  std::vector<uint8_t> buffer_;
};

const char *provisioning_frame_decode_status_name(
    ProvisioningFrameDecodeStatus status);

}  // namespace lunaredu::lunaros
