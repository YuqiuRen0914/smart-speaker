#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <string>

// Session-local wire metadata; conversation and audio remain owned by Runtime.
struct LunarLinkProtocol {
  int binding_epoch = 0;
  std::string device_id;
  std::string open_request;
  std::string activity_id;
  bool playback_ready = false;
  bool session_opened = false;
  uint32_t first_downlink = 0;
  uint32_t expected_last_downlink = 0;
  uint32_t played_packets = 0;
  uint32_t last_played_sequence = 0;
  bool playback_failed = false;
  bool terminal_seen = false;
  bool receipt_sent = false;
  std::string envelope(const std::string &type, const std::string &session,
                       JsonVariantConst payload, const std::string &request = {},
                       const std::string &correlation = {}) const;
  std::string open();
  // Existing local listen/abort intents are translated at the transport boundary.
  std::string encode_intent(const std::string &intent, uint32_t last_uplink);
  // Normalize only admitted messages into the existing conversation event vocabulary.
  bool decode(const std::string &message, const std::string &session,
              JsonDocument &normalized, std::string &ready_receipt);
  bool playback_delivered(bool received, bool output_error, bool binary_error) const;
  std::string playback_receipt(const std::string &session, bool delivered) const;
};
std::string lunarlink_next_id(const char *prefix);
bool lunarlink_header(JsonVariantConst message);
