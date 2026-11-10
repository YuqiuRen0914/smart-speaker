#include "lunaros_device_protocol.h"
#include "lunaros_device_frame.h"
#include "lunaredu_device_profiles.h"

#include <algorithm>
#include <utility>

namespace lunaredu::lunaros {
namespace {
static_assert(kDeviceProtocolVersion == protocol::kEnvelopeVersion);
static_assert(kDeviceProtocolMaxPayloadSize == protocol::kMaxPayloadBytes);

constexpr uint8_t kMagic0 = 0x4c;
constexpr uint8_t kMagic1 = 0x50;
constexpr size_t kHeaderSize = protocol::kHeaderBytes;
constexpr size_t kCrcSize = protocol::kCrcBytes;

uint16_t read_u16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(bytes[1] << 8);
}

bool has_magic_at(const std::vector<uint8_t> &buffer, size_t index) {
  return index + 1 < buffer.size() && buffer[index] == kMagic0 &&
         buffer[index + 1] == kMagic1;
}

struct ResponsePlan { size_t wire_limit = 0, chunk_bytes = 0, parts = 0; };
bool plan_response(const DeviceFrame &response, size_t max_wire_bytes,
                   ResponsePlan &plan) {
  constexpr size_t kWireOverhead = kHeaderSize + kCrcSize;
  constexpr size_t kEnvelopeReserve = 48;
  plan.wire_limit = std::min(max_wire_bytes,
      kDeviceProtocolMaxPayloadSize + kWireOverhead);
  if (response.payload.size() > protocol::profiles::fragment_max_response_bytes ||
      response.version != kDeviceProtocolVersion || response.type == 0 ||
      plan.wire_limit <= kWireOverhead) return false;
  if (response.payload.size() <= plan.wire_limit - kWireOverhead) {
    plan.parts = 1; return true;
  }
  if (plan.wire_limit <= kWireOverhead + kEnvelopeReserve + 1) return false;
  plan.chunk_bytes = std::min(protocol::profiles::fragment_max_part_bytes,
      (plan.wire_limit - kWireOverhead - kEnvelopeReserve) / 2);
  plan.parts = response.payload.size() / plan.chunk_bytes +
      (response.payload.size() % plan.chunk_bytes != 0);
  return plan.parts <= protocol::profiles::fragment_max_parts;
}

void append_text(std::vector<uint8_t> &out, const char *text) {
  while (*text) out.push_back(static_cast<uint8_t>(*text++));
}
void append_part_number(std::vector<uint8_t> &out, size_t number) {
  // Validated response cursors contain only indices/counts up to 256.
  char digits[3]; size_t count = 0;
  do { digits[count++] = static_cast<char>('0' + number % 10); number /= 10; }
  while (number);
  while (count) out.push_back(static_cast<uint8_t>(digits[--count]));
}
bool encode_response_part(const DeviceFrame &response,
                          const ResponsePlan &plan, size_t index,
                          std::vector<uint8_t> &encoded) {
  if (index >= plan.parts) { encoded.clear(); return false; }
  if (!plan.chunk_bytes) return encode_device_frame(response, encoded);
  constexpr char kHex[] = "0123456789abcdef";
  const size_t begin = index * plan.chunk_bytes;
  const size_t count = std::min(plan.chunk_bytes, response.payload.size() - begin);
  encoded.clear(); encoded.reserve(plan.wire_limit);
  encoded.resize(kHeaderSize);
  append_text(encoded, "{\"part\":"); append_part_number(encoded, index);
  append_text(encoded, ",\"total\":"); append_part_number(encoded, plan.parts);
  append_text(encoded, ",\"hex\":\"");
  for (size_t offset = 0; offset < count; ++offset) {
    const auto byte = static_cast<uint8_t>(response.payload[begin + offset]);
    encoded.push_back(kHex[byte >> 4]); encoded.push_back(kHex[byte & 15]);
  }
  append_text(encoded, "\"}");
  const size_t payload_bytes = encoded.size() - kHeaderSize;
  encoded.resize(encoded.size() + kCrcSize);
  size_t written = 0;
  return protocol::encode_frame(response.type, response.request_id,
      encoded.data() + kHeaderSize, payload_bytes, encoded.data(), encoded.size(), written) &&
      written <= plan.wire_limit;
}

}  // namespace

bool encode_device_frame(const DeviceFrame &frame,
                               std::vector<uint8_t> &encoded) {
  if (frame.version != kDeviceProtocolVersion || frame.type == 0 ||
      frame.payload.size() > kDeviceProtocolMaxPayloadSize ||
      frame.payload.size() > 0xffffu) {
    encoded.clear();
    return false;
  }

  encoded.resize(protocol::kOverheadBytes + frame.payload.size());
  size_t written = 0;
  const bool ok = protocol::encode_frame(frame.type, frame.request_id,
      reinterpret_cast<const uint8_t *>(frame.payload.data()), frame.payload.size(),
      encoded.data(), encoded.size(), written);
  if (!ok) encoded.clear();
  return ok;
}

bool write_device_response(const DeviceFrame &response,
                                size_t max_wire_bytes,
                                DeviceFrameWriter writer, void *context) {
  ResponsePlan plan;
  if (!writer || !plan_response(response, max_wire_bytes, plan)) return false;
  std::vector<uint8_t> encoded;
  for (size_t index = 0; index < plan.parts; ++index) {
    if (!encode_response_part(response, plan, index, encoded) ||
        !writer(encoded, context)) return false;
  }
  return true;
}

bool DeviceResponseEncoder::begin(DeviceFrame &&response,
                                        size_t max_wire_bytes) {
  if (pending()) return false;
  ResponsePlan plan;
  if (!plan_response(response, max_wire_bytes, plan)) return false;
  response_ = std::move(response);
  wire_limit_ = plan.wire_limit; chunk_bytes_ = plan.chunk_bytes;
  part_count_ = plan.parts; part_index_ = 0;
  return true;
}
bool DeviceResponseEncoder::encode_current(std::vector<uint8_t> &encoded) const {
  if (!pending()) { encoded.clear(); return false; }
  return encode_response_part(response_, {wire_limit_, chunk_bytes_, part_count_},
                              part_index_, encoded);
}
bool DeviceResponseEncoder::advance() {
  if (!pending()) return false;
  if (++part_index_ == part_count_) reset();
  return true;
}
void DeviceResponseEncoder::reset() {
  std::string{}.swap(response_.payload);
  response_.version = kDeviceProtocolVersion;
  response_.type = static_cast<uint8_t>(DeviceFrameType::GetInfo);
  response_.request_id = 0;
  wire_limit_ = chunk_bytes_ = part_count_ = part_index_ = 0;
}

DeviceFrameDecodeStatus DeviceFrameDecoder::append(
    const uint8_t *data, size_t length, std::vector<DeviceFrame> &frames) {
  frames.clear();
  if (data == nullptr && length != 0) {
    return DeviceFrameDecodeStatus::InvalidFrame;
  }
  if (length != 0) buffer_.insert(buffer_.end(), data, data + length);

  DeviceFrameDecodeStatus status = DeviceFrameDecodeStatus::NeedMore;
  while (true) {
    if (buffer_.size() < 2) return frames.empty() ? status
                                                 : DeviceFrameDecodeStatus::FrameReady;
    if (!has_magic_at(buffer_, 0)) {
      const auto magic = std::find(buffer_.begin() + 1, buffer_.end(), kMagic0);
      if (magic == buffer_.end()) {
        const uint8_t tail = buffer_.back();
        buffer_.clear();
        if (tail == kMagic0) buffer_.push_back(tail);
        if (status == DeviceFrameDecodeStatus::NeedMore)
          status = DeviceFrameDecodeStatus::InvalidMagic;
        return frames.empty() ? status
                              : DeviceFrameDecodeStatus::FrameReady;
      }
      buffer_.erase(buffer_.begin(), magic);
      if (status == DeviceFrameDecodeStatus::NeedMore)
        status = DeviceFrameDecodeStatus::InvalidMagic;
      continue;
    }
    if (buffer_.size() < kHeaderSize) return frames.empty() ? status
                                                            : DeviceFrameDecodeStatus::FrameReady;

    const uint8_t version = buffer_[2];
    if (version != kDeviceProtocolVersion) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
      status = DeviceFrameDecodeStatus::UnsupportedVersion;
      continue;
    }
    const uint16_t payload_size = read_u16(buffer_.data() + 8);
    if (payload_size > kDeviceProtocolMaxPayloadSize) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
      status = DeviceFrameDecodeStatus::PayloadTooLarge;
      continue;
    }
    const size_t frame_size = kHeaderSize + payload_size + kCrcSize;
    if (buffer_.size() < frame_size) {
      // Once a valid bounded header is known, reserve exactly that frame.
      // Streaming owners should not repeatedly grow/copy a second payload.
      buffer_.reserve(frame_size);
      return frames.empty() ? status : DeviceFrameDecodeStatus::FrameReady;
    }

    protocol::FrameView decoded;
    const auto result = protocol::decode_frame(buffer_.data(), frame_size, decoded);
    if (result != protocol::DecodeStatus::Ready) {
      buffer_.erase(buffer_.begin());
      status = result == protocol::DecodeStatus::InvalidCrc
          ? DeviceFrameDecodeStatus::InvalidCrc : DeviceFrameDecodeStatus::InvalidFrame;
      continue;
    }

    DeviceFrame frame;
    frame.version = version;
    frame.type = buffer_[3];
    frame.request_id = decoded.request_id;
    frame.payload.assign(reinterpret_cast<const char *>(buffer_.data() + kHeaderSize),
                         payload_size);
    frames.push_back(std::move(frame));
    buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
    status = DeviceFrameDecodeStatus::FrameReady;
  }
}

void DeviceFrameDecoder::reset() { buffer_.clear(); }

const char *device_frame_decode_status_name(
    DeviceFrameDecodeStatus status) {
  switch (status) {
    case DeviceFrameDecodeStatus::NeedMore:
      return "need_more";
    case DeviceFrameDecodeStatus::FrameReady:
      return "frame_ready";
    case DeviceFrameDecodeStatus::InvalidMagic:
      return "invalid_magic";
    case DeviceFrameDecodeStatus::UnsupportedVersion:
      return "unsupported_version";
    case DeviceFrameDecodeStatus::PayloadTooLarge:
      return "payload_too_large";
    case DeviceFrameDecodeStatus::InvalidCrc:
      return "invalid_crc";
    case DeviceFrameDecodeStatus::InvalidFrame:
      return "invalid_frame";
  }
  return "unknown";
}

}  // namespace lunaredu::lunaros
