#pragma once

#include <cstddef>
#include <cstdint>

namespace smart_speaker {

constexpr size_t kLogBufferCapacity = 96;
constexpr size_t kLogTextCapacity = 192;

struct BufferedLogEntry {
  uint32_t sequence = 0;
  uint32_t timestamp_ms = 0;
  uint16_t length = 0;
  char text[kLogTextCapacity]{};
};

struct LogBufferStatus {
  uint32_t oldest_sequence = 0;
  uint32_t latest_sequence = 0;
  uint32_t overwritten_entries = 0;
  uint32_t truncated_entries = 0;
  size_t count = 0;
};

// Tee application ESP_LOG output to a fixed RAM ring while preserving the
// previously configured USB Serial/JTAG console sink.
void setup_log_buffer();
LogBufferStatus log_buffer_status();
bool log_buffer_read(uint32_t sequence, BufferedLogEntry *entry);

} // namespace smart_speaker
