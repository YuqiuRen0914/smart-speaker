#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <aes/esp_aes.h>

struct XiaozhiMqttUdpConfig {
  std::string server;
  int port = 0;
  std::string key_hex;
  std::string nonce_hex;
};

enum class XiaozhiMqttUdpDecodeStatus {
  Ok,
  SequenceGap,
  StaleSequence,
  InvalidEnvelope,
  CryptoError,
  NotConfigured,
};

struct XiaozhiMqttUdpDecodeResult {
  XiaozhiMqttUdpDecodeStatus status =
      XiaozhiMqttUdpDecodeStatus::InvalidEnvelope;
  uint32_t sequence = 0;
  uint32_t expected_sequence = 0;
  uint32_t timestamp_ms = 0;
  std::vector<uint8_t> payload;
};

class XiaozhiMqttUdpChannel {
public:
  XiaozhiMqttUdpChannel();
  ~XiaozhiMqttUdpChannel();

  bool configure(const XiaozhiMqttUdpConfig &config);
  void reset();
  bool ready() const;

  const std::string &server() const { return server_; }
  int port() const { return port_; }

  bool encrypt_payload(const uint8_t *data, size_t length,
                       uint32_t timestamp_ms, std::string &datagram);
  XiaozhiMqttUdpDecodeResult decrypt_payload(const uint8_t *data,
                                             size_t length);

  int last_crypto_error() const { return last_crypto_error_; }

private:
  static constexpr size_t kCryptoChunkBytes = 4096;
  std::string server_;
  int port_ = 0;
  esp_aes_context aes_ctx_{};
  std::vector<uint8_t> base_nonce_;
  uint32_t local_sequence_ = 0;
  uint32_t remote_sequence_ = 0;
  bool configured_ = false;
  int last_crypto_error_ = 0;
  std::array<uint8_t, kCryptoChunkBytes> crypto_buffer_{};
  mutable std::mutex crypto_mutex_;
};
