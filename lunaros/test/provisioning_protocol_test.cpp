#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "lunaredu_lunaros/network/provisioning_protocol.h"

using namespace lunaredu::lunaros;

namespace {

ProvisioningFrame make_frame(uint32_t request_id, uint8_t type,
                             const char *payload) {
  ProvisioningFrame frame;
  frame.type = type;
  frame.request_id = request_id;
  frame.payload = payload;
  return frame;
}

void test_round_trip_and_fragmentation() {
  const ProvisioningFrame source = make_frame(
      42, static_cast<uint8_t>(ProvisioningFrameType::SetWifi),
      R"({"ssid":"Lunar","password":"secret"})");
  std::vector<uint8_t> encoded;
  assert(encode_provisioning_frame(source, encoded));

  ProvisioningFrameDecoder decoder;
  std::vector<ProvisioningFrame> frames;
  assert(decoder.append(encoded.data(), 4, frames) ==
         ProvisioningFrameDecodeStatus::NeedMore);
  assert(decoder.append(encoded.data() + 4, encoded.size() - 4, frames) ==
         ProvisioningFrameDecodeStatus::FrameReady);
  assert(frames.size() == 1);
  assert(frames[0].request_id == 42);
  assert(frames[0].type == static_cast<uint8_t>(ProvisioningFrameType::SetWifi));
  assert(frames[0].payload == source.payload);
}

void test_multiple_frames_and_noise_resynchronize() {
  std::vector<uint8_t> first;
  std::vector<uint8_t> second;
  assert(encode_provisioning_frame(make_frame(1, 1, "a"), first));
  assert(encode_provisioning_frame(make_frame(2, 2, "b"), second));
  std::vector<uint8_t> stream = {0x00, 0x7f};
  stream.insert(stream.end(), first.begin(), first.end());
  stream.insert(stream.end(), second.begin(), second.end());

  ProvisioningFrameDecoder decoder;
  std::vector<ProvisioningFrame> frames;
  assert(decoder.append(stream.data(), stream.size(), frames) ==
         ProvisioningFrameDecodeStatus::FrameReady);
  assert(frames.size() == 2);
  assert(frames[0].request_id == 1);
  assert(frames[1].request_id == 2);
}

void test_invalid_crc_and_payload_rejected() {
  std::vector<uint8_t> encoded;
  assert(encode_provisioning_frame(make_frame(7, 1, "payload"), encoded));
  encoded[encoded.size() - 1] ^= 0x01;
  ProvisioningFrameDecoder decoder;
  std::vector<ProvisioningFrame> frames;
  assert(decoder.append(encoded.data(), encoded.size(), frames) ==
         ProvisioningFrameDecodeStatus::InvalidCrc);
  assert(frames.empty());

  ProvisioningFrame oversized = make_frame(8, 1, "");
  oversized.payload.assign(kProvisioningProtocolMaxPayloadSize + 1, 'x');
  assert(!encode_provisioning_frame(oversized, encoded));
}

void test_header_validation_names() {
  ProvisioningFrame frame = make_frame(3, 1, "ok");
  frame.version = 2;
  std::vector<uint8_t> encoded;
  assert(!encode_provisioning_frame(frame, encoded));
  assert(std::string(provisioning_frame_decode_status_name(
             ProvisioningFrameDecodeStatus::InvalidCrc)) == "invalid_crc");
}

}  // namespace

int main() {
  test_round_trip_and_fragmentation();
  test_multiple_frames_and_noise_resynchronize();
  test_invalid_crc_and_payload_rejected();
  test_header_validation_names();
  std::cout << "provisioning protocol tests passed: 4\n";
  return 0;
}
