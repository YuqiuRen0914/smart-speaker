#include "interaction/transport/network_xiaozhi_mqtt_udp_channel.h"
#include <cassert>
#include <iostream>
#include <string>
std::string unhex(const std::string &hex) {
  std::string bytes;
  for (size_t i=0; i<hex.size(); i+=2) bytes.push_back(static_cast<char>(std::stoi(hex.substr(i,2),nullptr,16)));
  return bytes;
}
int main() {
  XiaozhiMqttUdpChannel channel;
  assert(channel.configure({"mac.local",18884,"00112233445566778899aabbccddeeff",
                            "01000000112233440000000000000000"}));
  const std::string text="voice protocol golden vector";
  const auto up=unhex("0100001c112233440000000700000080aca873d986ccb4b27db98bf6c31c2f2eeaf3d2906438d4b2fb17d115");
  const auto down=unhex("0101001c11223344000000070000008051e7c4c2034781561a2a579c5b362104d4dc74af17162e6f748c60a5");
  std::string encoded;
  assert(channel.encrypt_payload(reinterpret_cast<const uint8_t*>(text.data()),text.size(),7,encoded));
  assert(encoded==up && channel.last_sent_sequence()==128);
  auto receive=[&](const std::string &data) {return channel.decrypt_payload(reinterpret_cast<const uint8_t*>(data.data()),data.size());};
  assert(receive(up).status==XiaozhiMqttUdpDecodeStatus::InvalidEnvelope);
  auto altered=down; altered[4]^=1;
  assert(receive(altered).status==XiaozhiMqttUdpDecodeStatus::InvalidEnvelope);
  altered=down; altered[3]^=1;
  assert(receive(altered).status==XiaozhiMqttUdpDecodeStatus::InvalidEnvelope);
  auto result=receive(down);
  assert(result.status==XiaozhiMqttUdpDecodeStatus::Ok && result.timestamp_ms==7);
  assert(std::string(result.payload.begin(),result.payload.end())==text);
  assert(receive(down).status==XiaozhiMqttUdpDecodeStatus::StaleSequence);
  assert(channel.encrypt_payload(reinterpret_cast<const uint8_t*>(text.data()),text.size(),7,encoded));
  assert(channel.last_sent_sequence()==256 && encoded.substr(16)!=up.substr(16));
  std::string oversized(1469,'x');
  assert(!channel.encrypt_payload(reinterpret_cast<const uint8_t*>(oversized.data()),oversized.size(),7,encoded));
  std::cout<<"Mac UDP: Python golden vectors, turn, directions, replay, size and CTR block separation PASS\n";
}
