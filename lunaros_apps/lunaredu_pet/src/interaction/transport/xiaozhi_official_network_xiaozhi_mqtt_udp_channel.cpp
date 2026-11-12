// XiaoZhi transport retained from this repository baseline.
#include "interaction/transport/network_xiaozhi_mqtt_udp_channel.h"
#include "interaction/transport/assistant_provider.h"

#include <algorithm>
#include <cstring>

namespace {

uint8_t hex_to_byte(char ch) {
  if (ch >= '0' && ch <= '9') {
    return static_cast<uint8_t>(ch - '0');
  }
  if (ch >= 'A' && ch <= 'F') {
    return static_cast<uint8_t>(ch - 'A' + 10);
  }
  if (ch >= 'a' && ch <= 'f') {
    return static_cast<uint8_t>(ch - 'a' + 10);
  }
  return 0;
}

std::vector<uint8_t> decode_hex_bytes(const std::string &hex_string) {
  std::vector<uint8_t> decoded;
  if ((hex_string.size() % 2) != 0) {
    return decoded;
  }
  decoded.reserve(hex_string.size() / 2);
  for (size_t index = 0; index < hex_string.size(); index += 2) {
    decoded.push_back(static_cast<uint8_t>(
        (hex_to_byte(hex_string[index]) << 4) |
        hex_to_byte(hex_string[index + 1])));
  }
  return decoded;
}

void write_be16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[1] = static_cast<uint8_t>(value & 0xff);
}

void write_be32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>((value >> 24) & 0xff);
  data[1] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[2] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[3] = static_cast<uint8_t>(value & 0xff);
}

uint32_t read_be32(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

} // namespace

XiaozhiMqttUdpChannel::XiaozhiMqttUdpChannel() { esp_aes_init(&aes_ctx_); }

XiaozhiMqttUdpChannel::~XiaozhiMqttUdpChannel() { esp_aes_free(&aes_ctx_); }

bool XiaozhiMqttUdpChannel::configure(const XiaozhiMqttUdpConfig &config) {
  reset();

  const auto key = decode_hex_bytes(config.key_hex);
  const auto nonce = decode_hex_bytes(config.nonce_hex);
  if (config.server.empty() || config.port <= 0 || key.empty() ||
      nonce.size() < 16) {
    return false;
  }

  last_crypto_error_ = esp_aes_setkey(
      &aes_ctx_, key.data(), static_cast<unsigned int>(key.size() * 8));
  if (last_crypto_error_ != 0) {
    return false;
  }

  server_ = config.server;
  port_ = config.port;
  base_nonce_ = nonce;
  local_sequence_ = 0;
  remote_sequence_ = 0;
  configured_ = true;
  return true;
}

void XiaozhiMqttUdpChannel::reset() {
  server_.clear();
  port_ = 0;
  base_nonce_.clear();
  local_sequence_ = 0;
  remote_sequence_ = 0;
  configured_ = false;
  last_crypto_error_ = 0;
}

bool XiaozhiMqttUdpChannel::ready() const {
  return configured_ && !server_.empty() && port_ > 0 && !base_nonce_.empty();
}

bool XiaozhiMqttUdpChannel::encrypt_payload(const uint8_t *data, size_t length,
                                            uint32_t timestamp_ms,
                                            std::string &datagram) {
  datagram.clear();
  if (!ready() || data == nullptr || length == 0 || length > 0xffff || base_nonce_.size() < 16 ||
      (kAssistantUsesMac && (length > 1468 || local_sequence_ > UINT32_MAX - 256U))) {
    return false;
  }

  const size_t nonce_size = base_nonce_.size();
  datagram.resize(nonce_size + length);
  std::memcpy(datagram.data(), base_nonce_.data(), nonce_size);

  uint8_t* nonce_ptr = reinterpret_cast<uint8_t*>(datagram.data());
  write_be16(nonce_ptr + 2, static_cast<uint16_t>(length));
  write_be32(nonce_ptr + 8, timestamp_ms);
  // Mac uses timestamp for turn identity. Reserve 128 AES blocks per packet
  // so equal-size frames cannot overlap CTR keystream; separate directions.
  local_sequence_ += kAssistantUsesMac ? 128U : 1U;
  if (kAssistantUsesMac) nonce_ptr[1] = 0;
  write_be32(nonce_ptr + 12, local_sequence_);

  uint8_t iv[16];
  std::memcpy(iv, nonce_ptr, 16);

  size_t nc_off = 0;
  uint8_t stream_block[16] = {0};
  std::lock_guard<std::mutex> lock(crypto_mutex_);
  for (size_t offset = 0; offset < length;
       offset += std::min(kCryptoChunkBytes, length - offset)) {
    const size_t chunk = std::min(kCryptoChunkBytes, length - offset);
    std::memcpy(crypto_buffer_.data(), data + offset, chunk);
    last_crypto_error_ = esp_aes_crypt_ctr(
        &aes_ctx_, chunk, &nc_off, iv, stream_block, crypto_buffer_.data(),
        crypto_buffer_.data());
    if (last_crypto_error_ != 0) {
      datagram.clear();
      return false;
    }
    std::memcpy(&datagram[nonce_size + offset], crypto_buffer_.data(), chunk);
  }
  last_crypto_error_ = 0;
  return last_crypto_error_ == 0;
}

XiaozhiMqttUdpDecodeResult
XiaozhiMqttUdpChannel::decrypt_payload(const uint8_t *data, size_t length) {
  XiaozhiMqttUdpDecodeResult result;
  if (!ready()) {
    result.status = XiaozhiMqttUdpDecodeStatus::NotConfigured;
    return result;
  }
  if (data == nullptr || length < base_nonce_.size() || base_nonce_.size() < 16) {
    result.status = XiaozhiMqttUdpDecodeStatus::InvalidEnvelope;
    return result;
  }
  if (data[0] != 0x01) {
    result.status = XiaozhiMqttUdpDecodeStatus::InvalidEnvelope;
    return result;
  }
  if (kAssistantUsesMac && (data[1] != 1 || length > 1484 ||
      std::memcmp(data + 4, base_nonce_.data() + 4, 4) != 0 ||
      ((static_cast<size_t>(data[2]) << 8) | data[3]) != length - base_nonce_.size())) {
    result.status = XiaozhiMqttUdpDecodeStatus::InvalidEnvelope;
    return result;
  }

  result.timestamp_ms = read_be32(data + 8);
  result.sequence = read_be32(data + 12);
  result.expected_sequence = remote_sequence_ + (kAssistantUsesMac ? 128U : 1U);
  if (result.sequence < remote_sequence_ || (kAssistantUsesMac && result.sequence == remote_sequence_)) {
    result.status = XiaozhiMqttUdpDecodeStatus::StaleSequence;
    return result;
  }

  const size_t nonce_size = base_nonce_.size();
  const uint8_t *encrypted = data + nonce_size;
  const size_t encrypted_size = length - nonce_size;
  result.payload.resize(encrypted_size);

  uint8_t iv[16];
  std::memcpy(iv, data, 16);

  size_t nc_off = 0;
  uint8_t stream_block[16] = {0};
  std::lock_guard<std::mutex> lock(crypto_mutex_);
  for (size_t offset = 0; offset < encrypted_size;
       offset += std::min(kCryptoChunkBytes, encrypted_size - offset)) {
    const size_t chunk = std::min(kCryptoChunkBytes, encrypted_size - offset);
    std::memcpy(crypto_buffer_.data(), encrypted + offset, chunk);
    last_crypto_error_ = esp_aes_crypt_ctr(
        &aes_ctx_, chunk, &nc_off, iv, stream_block, crypto_buffer_.data(),
        crypto_buffer_.data());
    if (last_crypto_error_ != 0) {
      result.payload.clear();
      result.status = XiaozhiMqttUdpDecodeStatus::CryptoError;
      return result;
    }
    std::memcpy(result.payload.data() + offset, crypto_buffer_.data(), chunk);
  }
  last_crypto_error_ = 0;

  remote_sequence_ = result.sequence;
  result.status =
      (result.sequence == result.expected_sequence)
          ? XiaozhiMqttUdpDecodeStatus::Ok
          : XiaozhiMqttUdpDecodeStatus::SequenceGap;
  return result;
}
