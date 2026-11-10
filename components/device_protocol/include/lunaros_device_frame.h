#pragma once
#include "lunaros_crc32.h"
#include "lunaredu_device_contract.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lunaredu::protocol {
// Shared LP envelope. Message-domain allocation extends v1 without changing
// existing management packets. Remote profile v2 uses types 0x41..0x48.
constexpr uint8_t kEnvelopeVersion = contract::lp_version;
constexpr size_t kHeaderBytes = contract::lp_header_bytes, kCrcBytes = contract::lp_crc_bytes;
constexpr size_t kOverheadBytes = kHeaderBytes + kCrcBytes;
constexpr size_t kMaxPayloadBytes = contract::lp_max_payload_bytes;
enum class Domain { Management, RemoteControl, Presentation, Unassigned };
constexpr Domain message_domain(uint8_t type) {
  const auto base = static_cast<uint8_t>(type & 0x7f);
  if (base > 0 && (base < 0x40 || base == 0x7f)) return Domain::Management;
  // Remote messages do not use the management response bit.
  if (type >= 0x41 && type <= 0x45) return Domain::RemoteControl;
  if (type >= 0x46 && type <= 0x48) return Domain::Presentation;
  return Domain::Unassigned;
}
inline uint16_t read_u16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t read_u32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void write_u16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value); p[1] = static_cast<uint8_t>(value >> 8);
}
inline void write_u32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8));
}
struct FrameView {
  uint8_t type = 0;
  uint32_t request_id = 0;
  const uint8_t *payload = nullptr;
  size_t payload_size = 0;
  Domain domain() const { return message_domain(type); }
};
enum class DecodeStatus { Ready, InvalidMagic, UnsupportedVersion,
                          PayloadTooLarge, InvalidLength, InvalidCrc, InvalidType };
// Borrowed view is valid only while the caller retains the packet. Exact packet
// length is required; stream accumulation/resynchronization belongs to its owner.
inline DecodeStatus decode_frame(const uint8_t *data, size_t size, FrameView &out) {
  out = {};
  if (!data || size < kOverheadBytes) return DecodeStatus::InvalidLength;
  if (data[0] != 0x4c || data[1] != 0x50) return DecodeStatus::InvalidMagic;
  if (data[2] != kEnvelopeVersion) return DecodeStatus::UnsupportedVersion;
  const auto length = read_u16(data + 8);
  if (length > kMaxPayloadBytes) return DecodeStatus::PayloadTooLarge;
  if (size != kOverheadBytes + length) return DecodeStatus::InvalidLength;
  if (!data[3]) return DecodeStatus::InvalidType;
  if (read_u32(data + size - kCrcBytes) !=
      lunaredu::lunaros::digest::crc32(data, size - kCrcBytes)) return DecodeStatus::InvalidCrc;
  out = {data[3], read_u32(data + 4), data + kHeaderBytes, length};
  return DecodeStatus::Ready;
}
// No allocation, task, IO, clock or retry policy. Payload may already occupy
// out+kHeaderBytes; other overlap with out is not supported.
inline bool encode_frame(uint8_t type, uint32_t request_id,
                         const uint8_t *payload, size_t payload_size,
                         uint8_t *out, size_t capacity, size_t &written) {
  written = 0;
  if (!type || !out || (!payload && payload_size) || payload_size > kMaxPayloadBytes ||
      capacity < kOverheadBytes + payload_size) return false;
  out[0] = 0x4c; out[1] = 0x50; out[2] = kEnvelopeVersion; out[3] = type;
  write_u32(out + 4, request_id); write_u16(out + 8, static_cast<uint16_t>(payload_size));
  if (payload_size) std::memmove(out + kHeaderBytes, payload, payload_size);
  written = kOverheadBytes + payload_size;
  write_u32(out + written - kCrcBytes,
            lunaredu::lunaros::digest::crc32(out, written - kCrcBytes));
  return true;
}
} // namespace lunaredu::protocol
