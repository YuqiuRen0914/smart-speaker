#pragma once

#include "lunaredu_device_contract.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lunaredu::lunaros {

constexpr uint8_t kDeviceProtocolVersion = protocol::contract::lp_version;
constexpr size_t kDeviceProtocolMaxPayloadSize = protocol::contract::lp_max_payload_bytes;

enum class DeviceFrameType : uint8_t {
  GetInfo = protocol::contract::get_info,
  OpenSession = protocol::contract::open_session,
  ConfirmSession = protocol::contract::confirm_session,
  SetWifi = protocol::contract::set_wifi,
  GetStatus = protocol::contract::get_status,
  CloseSession = protocol::contract::close_session,
  // QR-confirmed BLE only: obtain a short-lived account binding handoff.
  GetBinding = protocol::contract::get_binding,
  FactoryStatus = protocol::contract::factory_status,
  FactoryStartTest = protocol::contract::factory_start,
  FactoryConfirmTest = protocol::contract::factory_confirm,
  FactoryResetTest = protocol::contract::factory_reset,
  FactorySetUnitId = protocol::contract::factory_unit_id,
  // Physical USB diagnostics and product controls. The payload is a JSON
  // object with an operation name; existing bootstrap frame types stay intact.
  DeviceCommand = protocol::contract::device_command,
  Error = protocol::contract::error,
};

constexpr uint8_t kDeviceFrameResponseFlag = 0x80;

struct DeviceFrame {
  uint8_t version = kDeviceProtocolVersion;
  uint8_t type = static_cast<uint8_t>(DeviceFrameType::GetInfo);
  uint32_t request_id = 0;
  std::string payload;
};

enum class DeviceFrameDecodeStatus : uint8_t {
  NeedMore,
  FrameReady,
  InvalidMagic,
  UnsupportedVersion,
  PayloadTooLarge,
  InvalidCrc,
  InvalidFrame,
};

bool encode_device_frame(const DeviceFrame &frame,
                               std::vector<uint8_t> &encoded);

using DeviceFrameWriter = bool (*)(const std::vector<uint8_t> &, void *);
// Demand-owned response cursor. begin consumes the frame only on success;
// a pending response cannot be replaced. encode_current is repeatable until
// advance acknowledges one whole accepted wire frame. Reset/final advance
// releases the payload; physical IO, backpressure and deadlines belong to USB
// or another transport owner. No worker, JSON library or clock is required.
class DeviceResponseEncoder {
 public:
  bool begin(DeviceFrame &&response, size_t max_wire_bytes);
  bool encode_current(std::vector<uint8_t> &encoded) const;
  bool advance();
  bool pending() const { return part_count_ != 0; }
  void reset();
  DeviceResponseEncoder() = default;
  DeviceResponseEncoder(const DeviceResponseEncoder &) = delete;
  DeviceResponseEncoder &operator=(const DeviceResponseEncoder &) = delete;
 private:
  DeviceFrame response_;
  size_t wire_limit_ = 0, chunk_bytes_ = 0, part_count_ = 0, part_index_ = 0;
};

// Emit one frame or bounded {part,total,hex} response chunks, synchronously.
// The caller supplies the response type/flag and owns the physical writer.
// Validate the complete response before writing; stop immediately on IO failure.
bool write_device_response(const DeviceFrame &response,
                                size_t max_wire_bytes,
                                DeviceFrameWriter writer, void *context);

class DeviceFrameDecoder {
 public:
  DeviceFrameDecodeStatus append(const uint8_t *data, size_t length,
                                        std::vector<DeviceFrame> &frames);
  void reset();

 private:
  std::vector<uint8_t> buffer_;
};

const char *device_frame_decode_status_name(
    DeviceFrameDecodeStatus status);

}  // namespace lunaredu::lunaros
