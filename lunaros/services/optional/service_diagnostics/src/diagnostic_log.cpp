#include "diagnostics/backend.h"
#include "capture_control.h"
#include "diagnostic_log_output.h"
#include "official_tools.h"
#include "lunaros_diagnostics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <new>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>

#include "kernel/static_mutex.h"

namespace lunaredu::lunaros::internal::capture {
namespace {


StaticMutex g_mutex;
using Records = std::array<DebugLogRecord, kDebugLogCapacity>;
std::unique_ptr<Records> g_records;
std::atomic<uint32_t> g_active_generation{0};
uint32_t g_generation = 0;
uint64_t g_next_sequence = 1;
uint64_t g_next_trace = 1;
size_t g_count = 0;
uint64_t g_overwritten=0,g_truncated=0;
struct LogWriter { vprintf_like_t invoke = nullptr; };
std::atomic<LogWriter> g_previous_writer{};
bool g_installed = false;
bool g_suspended = false;
struct DiagnosticOutputWriter { DiagnosticLogOutput invoke=nullptr; };
std::atomic<DiagnosticOutputWriter> g_output{};

bool sensitive_key_at(const char *text, size_t index, const char *key) {
  const size_t length = std::strlen(key);
  return std::strncmp(text + index, key, length) == 0;
}

void redact_sensitive_values(char *text) {
  if (text == nullptr) return;
  constexpr const char *keys[] = {
      "password=", "token=", "credential=", "debug_code=",
      "authorization:"};
  const size_t length = std::strlen(text);
  for (size_t index = 0; index < length; ++index) {
    size_t key_length = 0;
    for (const char *key : keys) {
      if (sensitive_key_at(text, index, key)) {
        key_length = std::strlen(key);
        break;
      }
    }
    if (key_length == 0) continue;
    size_t cursor = index + key_length;
    while (cursor < length && text[cursor] != ' ' && text[cursor] != '\r' &&
           text[cursor] != '\n' && text[cursor] != ',' &&
           text[cursor] != ';') {
      text[cursor++] = '*';
    }
    index = cursor;
  }
}

void strip_ansi_sequences(char *text) {
  if (text == nullptr) return;
  size_t read = 0;
  size_t write = 0;
  while (text[read] != '\0') {
    if (text[read] == '\x1b' && text[read + 1] == '[') {
      read += 2;
      while (text[read] != '\0' &&
             !((text[read] >= 'A' && text[read] <= 'Z') ||
               (text[read] >= 'a' && text[read] <= 'z'))) {
        ++read;
      }
      if (text[read] != '\0') ++read;
      continue;
    }
    text[write++] = text[read++];
  }
  text[write] = '\0';
}

void append_record(const char *message, uint32_t generation,bool truncated) {
  DebugLogRecord record;
  record.uptime_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  record.truncated=truncated;
  std::snprintf(record.message.data(), record.message.size(), "%s",
                message == nullptr ? "" : message);
  strip_ansi_sequences(record.message.data());
  redact_sensitive_values(record.message.data());

  StaticLockGuard lock(g_mutex);
  if (!g_records || generation != g_active_generation.load()) return;
  if(g_count==kDebugLogCapacity)++g_overwritten;
  if(record.truncated)++g_truncated;
  record.sequence = g_next_sequence++;
  (*g_records)[(record.sequence - 1) % kDebugLogCapacity] = record;
  g_count = std::min(g_count + 1, kDebugLogCapacity);
}

int capture_vprintf(const char *format, va_list arguments) {
  if(const auto output=g_output.load().invoke) {
    va_list copy;va_copy(copy,arguments);output(format,copy);va_end(copy);
  }
  const uint32_t generation = g_active_generation.load();
  if (generation) {
    char message[kDebugLogMessageBytes] = {};
    va_list copy;
    va_copy(copy, arguments);
    const int written=std::vsnprintf(message, sizeof(message), format, copy);
    va_end(copy);
    append_record(message,generation,written<0||size_t(written)>=sizeof(message));
  }
  const auto previous = g_previous_writer.load().invoke;
  return previous == nullptr ? 0 : previous(format, arguments);
}

} // namespace


DiagnosticTrace begin_diagnostic_trace(const char *operation, uint32_t request_sequence,
                                       uint64_t parent_id, DiagnosticPhase initial_phase) {
  if (!g_active_generation.load()) return {};
  DiagnosticTrace trace;
  {
    StaticLockGuard lock(g_mutex);
    if (!g_records) return {};
    trace = {g_next_trace++, g_generation, request_sequence, parent_id};
  }
  capture::record_diagnostic_trace(trace, initial_phase, operation);
  return trace;
}
bool diagnostic_capture_enabled() { return g_active_generation.load() != 0; }
bool diagnostic_trace_current(DiagnosticTrace trace) {
  return trace.id != 0 && trace.generation != 0 && g_active_generation.load() == trace.generation;
}
void record_diagnostic_trace(DiagnosticTrace trace, DiagnosticPhase phase,
                             const char *operation, const char *error) {
  if (!trace.id || g_active_generation.load() != trace.generation) return;
  DebugLogRecord record;
  record.trace_id = trace.id;
  record.parent_trace_id = trace.parent_id;
  record.request_sequence = trace.request_sequence;
  record.phase = phase;
  record.uptime_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  const auto copy = [&](size_t offset, const char *text) {
    if (!text) return;
    size_t i = 0;
    for (; text[i] && i < 79; ++i) {
      const char c = text[i];
      // Only stable code/identifier characters, never arbitrary request text.
      record.message[offset + i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ':') ? c : '?';
    }
    record.truncated = record.truncated || text[i] != '\0';
  };
  copy(0, operation);
  copy(80, error);
  StaticLockGuard lock(g_mutex);
  if (!g_records || g_generation != trace.generation) return;
  if(g_count==kDebugLogCapacity)++g_overwritten;
  if(record.truncated)++g_truncated;
  record.sequence = g_next_sequence++;
  (*g_records)[(record.sequence - 1) % kDebugLogCapacity] = record;
  g_count = std::min(g_count + 1, kDebugLogCapacity);
  official_tools::record(record,trace.generation);
}

void install_debug_log_collector() {
  StaticLockGuard lock(g_mutex);
  if (g_installed || g_suspended) return;
  const auto previous = esp_log_set_vprintf(capture_vprintf);
  g_previous_writer.store({previous == capture_vprintf ? nullptr : previous});
  g_installed = true;
}

void attach_diagnostic_log_output(DiagnosticLogOutput output) {
  g_output.store({output});resume_debug_log_collector();
}
void detach_diagnostic_log_output(DiagnosticLogOutput output) {
  DiagnosticOutputWriter expected{output};
  (void)g_output.compare_exchange_strong(expected,{});
}
void resume_debug_log_collector() {
  { StaticLockGuard lock(g_mutex);g_suspended=false; }
  install_debug_log_collector();
}
void uninstall_debug_log_collector() {
  StaticLockGuard lock(g_mutex);
  g_suspended=true;g_active_generation.store(0);g_records.reset();g_count=0;
  if(g_installed){esp_log_set_vprintf(g_previous_writer.load().invoke);g_installed=false;}
}

bool set_debug_log_capture(bool enabled) {
  install_debug_log_collector();
  StaticLockGuard lock(g_mutex);
  if (g_suspended) return !enabled;
  if (enabled == static_cast<bool>(g_records)) return true;
  if (!enabled) {
    g_active_generation.store(0);
    g_records.reset();
    g_count = 0;
    return true;
  }
  // Refuse generation reuse rather than admitting an old in-flight log.
  if (g_generation == UINT32_MAX) return false;
  std::unique_ptr<Records> records(new (std::nothrow) Records{});
  if (!records) return false;
  g_records = std::move(records);
  g_count = 0;g_overwritten=0;g_truncated=0;
  g_active_generation.store(++g_generation);
  return true;
}

DebugLogCaptureStatus debug_log_capture_status() {
  StaticLockGuard lock(g_mutex);
  return {static_cast<bool>(g_records), g_generation,
          g_records ? sizeof(Records) : 0,g_count,kDebugLogCapacity,
          g_count?g_next_sequence-g_count:0,g_count?g_next_sequence-1:0,g_overwritten,g_truncated};
}

DebugLogReadResult read_debug_logs(uint64_t after, size_t limit, bool from_oldest) {
  DebugLogReadResult result;
  limit = std::clamp<size_t>(limit, 1, kDebugLogReadCapacity);
  StaticLockGuard lock(g_mutex);
  result.enabled = static_cast<bool>(g_records);
  result.generation = g_generation;
  const uint64_t next = g_next_sequence;
  const uint64_t oldest = g_count == 0 ? next : next - g_count;
  result.oldest_cursor = oldest;
  result.next_cursor = next;
  if (after >= next && after != 0) {
    result.cursor_reset = true;
    after = 0;
  }
  if (after != 0 && after < oldest - (oldest > 0 ? 1 : 0)) {
    result.cursor_reset = true;
    after = oldest - (oldest > 0 ? 1 : 0);
  }
  uint64_t first = from_oldest ? oldest : after == 0 ? (next > limit ? next - limit : oldest)
                              : std::max(after + 1, oldest);
  if (first < oldest) first = oldest;
  for (uint64_t sequence = first;
       sequence < next && result.count < limit; ++sequence) {
    const DebugLogRecord &record =
        (*g_records)[(sequence - 1) % kDebugLogCapacity];
    if (record.sequence == sequence) result.records[result.count++] = record;
  }
  return result;
}

} // namespace lunaredu::lunaros::internal

namespace lunaredu::lunaros::internal {
bool bind_log_capture_backend() {
 static const DiagnosticLogBackend backend{capture::install_debug_log_collector,capture::uninstall_debug_log_collector,
  capture::set_debug_log_capture,capture::debug_log_capture_status,capture::read_debug_logs,
  capture::diagnostic_capture_enabled,capture::diagnostic_trace_current,capture::begin_diagnostic_trace,capture::record_diagnostic_trace};
 return bind_diagnostic_log_backend(backend);
}
void resume_debug_log_collector(){capture::resume_debug_log_collector();}
}

namespace lunaredu::lunaros {
bool start_diagnostics_boot_capture(std::string &error){
  if(!internal::bind_log_capture_backend()){error="diagnostic_log_backend_conflict";return false;}
  internal::resume_debug_log_collector();
  if(!set_debug_log_capture(true)){error="diagnostic_log_allocation_failed";return false;}
  error.clear();return true;
}
}
