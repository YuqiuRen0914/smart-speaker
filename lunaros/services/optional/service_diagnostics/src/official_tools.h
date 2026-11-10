#pragma once
#include <cstdint>
#include <string>
#include "sdkconfig.h"
#include "diagnostics/log.h"

namespace lunaredu::lunaros::internal::official_tools {
struct Status {
  const char *trace_mode = "none";
  const char *core_dump = "none";
  bool available = false;
  bool active = false;
  bool retiring = false;
  bool host_connected = false;
  uint32_t generation = 0;
  uint32_t remaining_ms = 0;
  uint32_t emitted = 0;
  uint32_t dropped = 0;
  int last_error = 0;
};
#if CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE || CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW || CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_UART || CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_FLASH
void resume();
bool shutdown(std::string &error);
bool start(uint32_t duration_ms, std::string &error, uint64_t authorization_deadline_ms=UINT64_MAX);
bool stop(std::string &error);
bool needs_poll();
void poll(bool session_active);
Status status();
// Called only after the existing collector validated its generation and sanitized
// the operation. Never emit arbitrary logs, request bodies, credentials or audio.
void record(const DebugLogRecord &record, uint32_t capture_generation);
#else
inline void resume() {}
inline bool shutdown(std::string &error) { error.clear(); return true; }
inline bool start(uint32_t, std::string &error, uint64_t=UINT64_MAX) { error="official_trace_not_configured"; return false; }
inline bool stop(std::string &error) { error.clear(); return true; }
inline bool needs_poll() { return false; }
inline void poll(bool) {}
inline Status status() { return {}; }
inline void record(const DebugLogRecord &, uint32_t) {}
#endif
}
