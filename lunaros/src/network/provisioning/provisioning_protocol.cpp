#include "lunaredu_lunaros/network/provisioning_protocol.h"

#include <algorithm>
#include <utility>

namespace lunaredu::lunaros {
namespace {

constexpr uint8_t kMagic0 = 0x4c;
constexpr uint8_t kMagic1 = 0x50;
constexpr size_t kHeaderSize = 10;
constexpr size_t kCrcSize = 4;

uint16_t read_u16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t read_u32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

void write_u16(std::vector<uint8_t> &bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value & 0xff));
  bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void write_u32(std::vector<uint8_t> &bytes, uint32_t value) {
  bytes.push_back(static_cast<uint8_t>(value & 0xff));
  bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

uint32_t crc32(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
  }
  return crc ^ 0xffffffffu;
}

bool has_magic_at(const std::vector<uint8_t> &buffer, size_t index) {
  return index + 1 < buffer.size() && buffer[index] == kMagic0 &&
         buffer[index + 1] == kMagic1;
}

}  // namespace

bool encode_provisioning_frame(const ProvisioningFrame &frame,
                               std::vector<uint8_t> &encoded) {
  if (frame.version != kProvisioningProtocolVersion || frame.type == 0 ||
      frame.payload.size() > kProvisioningProtocolMaxPayloadSize ||
      frame.payload.size() > 0xffffu) {
    encoded.clear();
    return false;
  }

  encoded.clear();
  encoded.reserve(kHeaderSize + frame.payload.size() + kCrcSize);
  encoded.push_back(kMagic0);
  encoded.push_back(kMagic1);
  encoded.push_back(frame.version);
  encoded.push_back(frame.type);
  write_u32(encoded, frame.request_id);
  write_u16(encoded, static_cast<uint16_t>(frame.payload.size()));
  encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
  write_u32(encoded, crc32(encoded.data(), encoded.size()));
  return true;
}

ProvisioningFrameDecodeStatus ProvisioningFrameDecoder::append(
    const uint8_t *data, size_t length, std::vector<ProvisioningFrame> &frames) {
  if (data == nullptr && length != 0) {
    return ProvisioningFrameDecodeStatus::InvalidFrame;
  }
  frames.clear();
  if (length != 0) buffer_.insert(buffer_.end(), data, data + length);

  ProvisioningFrameDecodeStatus status = ProvisioningFrameDecodeStatus::NeedMore;
  while (true) {
    if (buffer_.size() < 2) return frames.empty() ? status
                                                 : ProvisioningFrameDecodeStatus::FrameReady;
    if (!has_magic_at(buffer_, 0)) {
      const auto magic = std::find(buffer_.begin() + 1, buffer_.end(), kMagic0);
      if (magic == buffer_.end()) {
        const uint8_t tail = buffer_.back();
        buffer_.clear();
        if (tail == kMagic0) buffer_.push_back(tail);
        status = ProvisioningFrameDecodeStatus::InvalidMagic;
        return frames.empty() ? status
                              : ProvisioningFrameDecodeStatus::FrameReady;
      }
      buffer_.erase(buffer_.begin(), magic);
      status = ProvisioningFrameDecodeStatus::InvalidMagic;
      continue;
    }
    if (buffer_.size() < kHeaderSize) return frames.empty() ? status
                                                            : ProvisioningFrameDecodeStatus::FrameReady;

    const uint8_t version = buffer_[2];
    if (version != kProvisioningProtocolVersion) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
      status = ProvisioningFrameDecodeStatus::UnsupportedVersion;
      continue;
    }
    const uint16_t payload_size = read_u16(buffer_.data() + 8);
    if (payload_size > kProvisioningProtocolMaxPayloadSize) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
      status = ProvisioningFrameDecodeStatus::PayloadTooLarge;
      continue;
    }
    const size_t frame_size = kHeaderSize + payload_size + kCrcSize;
    if (buffer_.size() < frame_size) return frames.empty() ? status
                                                           : ProvisioningFrameDecodeStatus::FrameReady;

    const uint32_t expected_crc = read_u32(buffer_.data() + frame_size - kCrcSize);
    const uint32_t actual_crc = crc32(buffer_.data(), frame_size - kCrcSize);
    if (expected_crc != actual_crc) {
      buffer_.erase(buffer_.begin());
      return frames.empty() ? ProvisioningFrameDecodeStatus::InvalidCrc
                            : ProvisioningFrameDecodeStatus::FrameReady;
    }

    ProvisioningFrame frame;
    frame.version = version;
    frame.type = buffer_[3];
    frame.request_id = read_u32(buffer_.data() + 4);
    frame.payload.assign(reinterpret_cast<const char *>(buffer_.data() + kHeaderSize),
                         payload_size);
    frames.push_back(std::move(frame));
    buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
    status = ProvisioningFrameDecodeStatus::FrameReady;
  }
}

void ProvisioningFrameDecoder::reset() { buffer_.clear(); }

const char *provisioning_frame_decode_status_name(
    ProvisioningFrameDecodeStatus status) {
  switch (status) {
    case ProvisioningFrameDecodeStatus::NeedMore:
      return "need_more";
    case ProvisioningFrameDecodeStatus::FrameReady:
      return "frame_ready";
    case ProvisioningFrameDecodeStatus::InvalidMagic:
      return "invalid_magic";
    case ProvisioningFrameDecodeStatus::UnsupportedVersion:
      return "unsupported_version";
    case ProvisioningFrameDecodeStatus::PayloadTooLarge:
      return "payload_too_large";
    case ProvisioningFrameDecodeStatus::InvalidCrc:
      return "invalid_crc";
    case ProvisioningFrameDecodeStatus::InvalidFrame:
      return "invalid_frame";
  }
  return "unknown";
}

}  // namespace lunaredu::lunaros
