#include "smart_speaker_log_buffer.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <esp_log_write.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

namespace smart_speaker {
namespace {

BufferedLogEntry g_entries[kLogBufferCapacity]{};
portMUX_TYPE g_log_lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t g_console_vprintf = &vprintf;
uint32_t g_latest_sequence = 0;
uint32_t g_overwritten_entries = 0;
uint32_t g_truncated_entries = 0;
size_t g_count = 0;
bool g_installed = false;

void append_log(const char *text, size_t length, bool truncated) {
  if (text == nullptr || length == 0) return;
  while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
    --length;
  if (length == 0) return;
  const uint32_t timestamp_ms =
      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

  portENTER_CRITICAL(&g_log_lock);
  const uint32_t sequence = ++g_latest_sequence;
  BufferedLogEntry &entry = g_entries[(sequence - 1) % kLogBufferCapacity];
  entry.sequence = sequence;
  entry.timestamp_ms = timestamp_ms;
  entry.length = static_cast<uint16_t>(length);
  std::memcpy(entry.text, text, length);
  entry.text[length] = '\0';
  if (g_count < kLogBufferCapacity)
    ++g_count;
  else
    ++g_overwritten_entries;
  if (truncated) ++g_truncated_entries;
  portEXIT_CRITICAL(&g_log_lock);
}

int capture_vprintf(const char *format, va_list arguments) {
  va_list console_arguments;
  va_copy(console_arguments, arguments);
  const int written = g_console_vprintf(format, console_arguments);
  va_end(console_arguments);

  char text[kLogTextCapacity];
  va_list capture_arguments;
  va_copy(capture_arguments, arguments);
  const int required =
      std::vsnprintf(text, sizeof(text), format, capture_arguments);
  va_end(capture_arguments);
  if (required > 0) {
    const size_t length = std::min(static_cast<size_t>(required),
                                   sizeof(text) - 1);
    append_log(text, length, static_cast<size_t>(required) >= sizeof(text));
  }
  return written;
}

} // namespace

void setup_log_buffer() {
  if (g_installed) return;
  g_console_vprintf = esp_log_set_vprintf(capture_vprintf);
  g_installed = true;
}

LogBufferStatus log_buffer_status() {
  portENTER_CRITICAL(&g_log_lock);
  LogBufferStatus status;
  status.latest_sequence = g_latest_sequence;
  status.count = g_count;
  status.oldest_sequence =
      g_count == 0 ? 0 : g_latest_sequence - static_cast<uint32_t>(g_count) + 1;
  status.overwritten_entries = g_overwritten_entries;
  status.truncated_entries = g_truncated_entries;
  portEXIT_CRITICAL(&g_log_lock);
  return status;
}

bool log_buffer_read(uint32_t sequence, BufferedLogEntry *entry) {
  if (entry == nullptr || sequence == 0) return false;
  portENTER_CRITICAL(&g_log_lock);
  const BufferedLogEntry &candidate =
      g_entries[(sequence - 1) % kLogBufferCapacity];
  const bool found = candidate.sequence == sequence;
  if (found) *entry = candidate;
  portEXIT_CRITICAL(&g_log_lock);
  return found;
}

} // namespace smart_speaker
