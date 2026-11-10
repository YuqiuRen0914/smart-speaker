// Generated from protocol/remote-control/v2/wire.json. Run node tools/repo/generators/generate-remote-codec.mjs.
#pragma once
#include "lunaros_device_frame.h"
#include "lunaredu_remote_control_presentation.h"
#include <array>
#include <limits>
#include <type_traits>
namespace lunaredu::remote_control {
constexpr unsigned kRemoteWireProfileVersion = 2;
constexpr size_t kRemoteMaxWireBytes = 250;
namespace wire_detail {
template<class T> void put(uint8_t *&p, T value) {
  using U = std::make_unsigned_t<T>;
  const auto bits = static_cast<U>(value);
  for (size_t i=0;i<sizeof(T);++i) *p++ = static_cast<uint8_t>(bits >> (8*i));
}
template<class T> T get(const uint8_t *&p) {
  using U = std::make_unsigned_t<T>;
  U bits = 0;
  for (size_t i=0;i<sizeof(T);++i) bits |= static_cast<U>(*p++) << (8*i);
  if constexpr (std::is_signed_v<T>) {
    if (bits <= static_cast<U>(std::numeric_limits<T>::max())) return static_cast<T>(bits);
    return static_cast<T>(-1 - static_cast<T>(static_cast<U>(~bits)));
  }
  else return bits;
}
}
template<class T> struct RemoteFrameTraits;
template<> struct RemoteFrameTraits<InputFrame> {
  static constexpr uint8_t type = 65;
  static constexpr size_t payload_bytes = 34;
  static void encode(const InputFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint16_t>(f.flags));
    wire_detail::put(p, static_cast<uint32_t>(f.sequence));
    wire_detail::put(p, static_cast<uint32_t>(f.timestamp_ms));
    wire_detail::put(p, static_cast<int16_t>(f.left_x));
    wire_detail::put(p, static_cast<int16_t>(f.left_y));
    wire_detail::put(p, static_cast<int16_t>(f.right_x));
    wire_detail::put(p, static_cast<int16_t>(f.right_y));
    wire_detail::put(p, static_cast<int16_t>(f.encoder_delta));
    wire_detail::put(p, static_cast<int16_t>(f.encoder_position));
    wire_detail::put(p, static_cast<uint16_t>(f.buttons));
    wire_detail::put(p, static_cast<uint16_t>(f.reserved));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
  }
  static void decode(const uint8_t *p, InputFrame &f) {
    f.flags = wire_detail::get<uint16_t>(p);
    f.sequence = wire_detail::get<uint32_t>(p);
    f.timestamp_ms = wire_detail::get<uint32_t>(p);
    f.left_x = wire_detail::get<int16_t>(p);
    f.left_y = wire_detail::get<int16_t>(p);
    f.right_x = wire_detail::get<int16_t>(p);
    f.right_y = wire_detail::get<int16_t>(p);
    f.encoder_delta = wire_detail::get<int16_t>(p);
    f.encoder_position = wire_detail::get<int16_t>(p);
    f.buttons = wire_detail::get<uint16_t>(p);
    f.reserved = wire_detail::get<uint16_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
  }
};
template<> struct RemoteFrameTraits<PairingFrame> {
  static constexpr uint8_t type = 66;
  static constexpr size_t payload_bytes = 22;
  static void encode(const PairingFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint8_t>(f.message));
    wire_detail::put(p, static_cast<uint8_t>(f.role));
    wire_detail::put(p, static_cast<uint8_t>(f.channel));
    for(size_t i=0;i<3;++i) wire_detail::put(p, static_cast<uint8_t>(f.reserved[i]));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
    wire_detail::put(p, static_cast<uint32_t>(f.timestamp_ms));
    wire_detail::put(p, static_cast<uint32_t>(f.config_revision));
  }
  static void decode(const uint8_t *p, PairingFrame &f) {
    f.message = wire_detail::get<uint8_t>(p);
    f.role = wire_detail::get<uint8_t>(p);
    f.channel = wire_detail::get<uint8_t>(p);
    for(size_t i=0;i<3;++i) f.reserved[i] = wire_detail::get<uint8_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
    f.timestamp_ms = wire_detail::get<uint32_t>(p);
    f.config_revision = wire_detail::get<uint32_t>(p);
  }
};
template<> struct RemoteFrameTraits<LinkStatusFrame> {
  static constexpr uint8_t type = 67;
  static constexpr size_t payload_bytes = 26;
  static void encode(const LinkStatusFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint8_t>(f.state));
    wire_detail::put(p, static_cast<uint8_t>(f.reserved0));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
    wire_detail::put(p, static_cast<uint32_t>(f.accepted_frames));
    wire_detail::put(p, static_cast<uint32_t>(f.rejected_frames));
    wire_detail::put(p, static_cast<int8_t>(f.rssi));
    for(size_t i=0;i<7;++i) wire_detail::put(p, static_cast<uint8_t>(f.reserved[i]));
  }
  static void decode(const uint8_t *p, LinkStatusFrame &f) {
    f.state = wire_detail::get<uint8_t>(p);
    f.reserved0 = wire_detail::get<uint8_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
    f.accepted_frames = wire_detail::get<uint32_t>(p);
    f.rejected_frames = wire_detail::get<uint32_t>(p);
    f.rssi = wire_detail::get<int8_t>(p);
    for(size_t i=0;i<7;++i) f.reserved[i] = wire_detail::get<uint8_t>(p);
  }
};
template<> struct RemoteFrameTraits<CalibrationCommandFrame> {
  static constexpr uint8_t type = 68;
  static constexpr size_t payload_bytes = 18;
  static void encode(const CalibrationCommandFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint8_t>(f.command));
    wire_detail::put(p, static_cast<uint8_t>(f.reserved));
    wire_detail::put(p, static_cast<uint32_t>(f.sequence));
    wire_detail::put(p, static_cast<uint32_t>(f.timestamp_ms));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
  }
  static void decode(const uint8_t *p, CalibrationCommandFrame &f) {
    f.command = wire_detail::get<uint8_t>(p);
    f.reserved = wire_detail::get<uint8_t>(p);
    f.sequence = wire_detail::get<uint32_t>(p);
    f.timestamp_ms = wire_detail::get<uint32_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
  }
};
template<> struct RemoteFrameTraits<ControllerStatusFrame> {
  static constexpr uint8_t type = 69;
  static constexpr size_t payload_bytes = 78;
  static void encode(const ControllerStatusFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint8_t>(f.calibration_state));
    wire_detail::put(p, static_cast<uint8_t>(f.calibration_error));
    wire_detail::put(p, static_cast<uint32_t>(f.sequence));
    wire_detail::put(p, static_cast<uint32_t>(f.last_command_sequence));
    wire_detail::put(p, static_cast<uint32_t>(f.timestamp_ms));
    wire_detail::put(p, static_cast<uint32_t>(f.profile_revision));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
    for(size_t i=0;i<4;++i) wire_detail::put(p, static_cast<uint16_t>(f.raw_axes[i]));
    for(size_t i=0;i<4;++i) wire_detail::put(p, static_cast<int16_t>(f.normalized_axes[i]));
    for(size_t i=0;i<4;++i) wire_detail::put(p, static_cast<uint16_t>(f.minimum[i]));
    for(size_t i=0;i<4;++i) wire_detail::put(p, static_cast<uint16_t>(f.center[i]));
    for(size_t i=0;i<4;++i) wire_detail::put(p, static_cast<uint16_t>(f.maximum[i]));
    for(size_t i=0;i<4;++i) wire_detail::put(p, static_cast<uint16_t>(f.deadzone[i]));
    wire_detail::put(p, static_cast<uint16_t>(f.sample_count));
    wire_detail::put(p, static_cast<uint8_t>(f.profile_valid));
    wire_detail::put(p, static_cast<uint8_t>(f.digital_levels));
  }
  static void decode(const uint8_t *p, ControllerStatusFrame &f) {
    f.calibration_state = wire_detail::get<uint8_t>(p);
    f.calibration_error = wire_detail::get<uint8_t>(p);
    f.sequence = wire_detail::get<uint32_t>(p);
    f.last_command_sequence = wire_detail::get<uint32_t>(p);
    f.timestamp_ms = wire_detail::get<uint32_t>(p);
    f.profile_revision = wire_detail::get<uint32_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
    for(size_t i=0;i<4;++i) f.raw_axes[i] = wire_detail::get<uint16_t>(p);
    for(size_t i=0;i<4;++i) f.normalized_axes[i] = wire_detail::get<int16_t>(p);
    for(size_t i=0;i<4;++i) f.minimum[i] = wire_detail::get<uint16_t>(p);
    for(size_t i=0;i<4;++i) f.center[i] = wire_detail::get<uint16_t>(p);
    for(size_t i=0;i<4;++i) f.maximum[i] = wire_detail::get<uint16_t>(p);
    for(size_t i=0;i<4;++i) f.deadzone[i] = wire_detail::get<uint16_t>(p);
    f.sample_count = wire_detail::get<uint16_t>(p);
    f.profile_valid = wire_detail::get<uint8_t>(p);
    f.digital_levels = wire_detail::get<uint8_t>(p);
  }
};
template<> struct RemoteFrameTraits<DeviceProfileFrame> {
  static constexpr uint8_t type = 70;
  static constexpr size_t payload_bytes = 88;
  static void encode(const DeviceProfileFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint8_t>(f.channel));
    wire_detail::put(p, static_cast<uint8_t>(f.icon_format));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
    wire_detail::put(p, static_cast<uint32_t>(f.icon_revision));
    wire_detail::put(p, static_cast<uint32_t>(f.icon_crc32));
    wire_detail::put(p, static_cast<uint16_t>(f.icon_bytes));
    wire_detail::put(p, static_cast<uint16_t>(f.foreground_rgb565));
    wire_detail::put(p, static_cast<uint8_t>(f.icon_width));
    wire_detail::put(p, static_cast<uint8_t>(f.icon_height));
    for(size_t i=0;i<16;++i) wire_detail::put(p, static_cast<uint8_t>(f.manufacturer[i]));
    for(size_t i=0;i<24;++i) wire_detail::put(p, static_cast<uint8_t>(f.product[i]));
    for(size_t i=0;i<24;++i) wire_detail::put(p, static_cast<uint8_t>(f.display_name[i]));
  }
  static void decode(const uint8_t *p, DeviceProfileFrame &f) {
    f.channel = wire_detail::get<uint8_t>(p);
    f.icon_format = wire_detail::get<uint8_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
    f.icon_revision = wire_detail::get<uint32_t>(p);
    f.icon_crc32 = wire_detail::get<uint32_t>(p);
    f.icon_bytes = wire_detail::get<uint16_t>(p);
    f.foreground_rgb565 = wire_detail::get<uint16_t>(p);
    f.icon_width = wire_detail::get<uint8_t>(p);
    f.icon_height = wire_detail::get<uint8_t>(p);
    for(size_t i=0;i<16;++i) f.manufacturer[i] = static_cast<char>(wire_detail::get<uint8_t>(p));
    for(size_t i=0;i<24;++i) f.product[i] = static_cast<char>(wire_detail::get<uint8_t>(p));
    for(size_t i=0;i<24;++i) f.display_name[i] = static_cast<char>(wire_detail::get<uint8_t>(p));
  }
};
template<> struct RemoteFrameTraits<AssetRequestFrame> {
  static constexpr uint8_t type = 71;
  static constexpr size_t payload_bytes = 14;
  static void encode(const AssetRequestFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint16_t>(f.offset));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
    wire_detail::put(p, static_cast<uint32_t>(f.icon_revision));
  }
  static void decode(const uint8_t *p, AssetRequestFrame &f) {
    f.offset = wire_detail::get<uint16_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
    f.icon_revision = wire_detail::get<uint32_t>(p);
  }
};
template<> struct RemoteFrameTraits<IconChunkFrame> {
  static constexpr uint8_t type = 72;
  static constexpr size_t payload_bytes = 208;
  static void encode(const IconChunkFrame &f, uint8_t *p) {
    wire_detail::put(p, static_cast<uint16_t>(f.offset));
    wire_detail::put(p, static_cast<uint64_t>(f.binding_nonce));
    wire_detail::put(p, static_cast<uint32_t>(f.icon_revision));
    wire_detail::put(p, static_cast<uint16_t>(f.length));
    for(size_t i=0;i<192;++i) wire_detail::put(p, static_cast<uint8_t>(f.data[i]));
  }
  static void decode(const uint8_t *p, IconChunkFrame &f) {
    f.offset = wire_detail::get<uint16_t>(p);
    f.binding_nonce = wire_detail::get<uint64_t>(p);
    f.icon_revision = wire_detail::get<uint32_t>(p);
    f.length = wire_detail::get<uint16_t>(p);
    for(size_t i=0;i<192;++i) f.data[i] = wire_detail::get<uint8_t>(p);
  }
};
struct RemotePacket {
  std::array<uint8_t,kRemoteMaxWireBytes> bytes{};
  size_t size = 0;
};
template<class T> bool encode_remote_frame(const T &frame, RemotePacket &out) {
  using Traits = RemoteFrameTraits<T>;
  static_assert(Traits::payload_bytes + protocol::kOverheadBytes <= kRemoteMaxWireBytes);
  auto *payload = out.bytes.data() + protocol::kHeaderBytes;
  Traits::encode(frame, payload);
  return protocol::encode_frame(Traits::type, 0, payload, Traits::payload_bytes,
      out.bytes.data(), out.bytes.size(), out.size);
}
inline bool valid_remote_chunk(const IconChunkFrame &f) {
  return f.binding_nonce && f.icon_revision && f.offset < kMaxIconBytes &&
      f.offset % kIconChunkBytes == 0 && f.length > 0 && f.length <= kIconChunkBytes;
}
template<class T> bool decode_remote_frame(const protocol::FrameView &view, T &out) {
  out = {};
  if (view.type != RemoteFrameTraits<T>::type || view.request_id ||
      !view.payload || view.payload_size != RemoteFrameTraits<T>::payload_bytes) return false;
  T candidate;
  RemoteFrameTraits<T>::decode(view.payload, candidate);
  bool valid;
  if constexpr (std::is_same_v<T,InputFrame>) valid = valid_input_frame(candidate);
  else if constexpr (std::is_same_v<T,PairingFrame>) valid = valid_pairing_frame(candidate);
  else if constexpr (std::is_same_v<T,LinkStatusFrame>) valid = valid_link_status_frame(candidate);
  else if constexpr (std::is_same_v<T,CalibrationCommandFrame>) valid = valid_calibration_command_frame(candidate);
  else if constexpr (std::is_same_v<T,ControllerStatusFrame>) valid = valid_controller_status_frame(candidate);
  else if constexpr (std::is_same_v<T,DeviceProfileFrame>) valid = valid_device_profile(candidate);
  else if constexpr (std::is_same_v<T,AssetRequestFrame>) valid = valid_asset_request(candidate);
  else valid = valid_remote_chunk(candidate);
  if (valid) out = candidate;
  return valid;
}
template<class T> bool decode_remote_frame(const uint8_t *data, size_t size, T &out) {
  protocol::FrameView view;
  if (protocol::decode_frame(data,size,view) != protocol::DecodeStatus::Ready) { out={}; return false; }
  return decode_remote_frame(view,out);
}
template<class T, class Writer> bool write_remote_frame(const T &frame, Writer writer) {
  RemotePacket packet;
  return encode_remote_frame(frame,packet) && writer(packet.bytes.data(),packet.size);
}
} // namespace lunaredu::remote_control
