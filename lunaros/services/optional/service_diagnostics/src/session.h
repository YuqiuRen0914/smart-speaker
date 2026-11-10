#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::diagnostics {
bool start_sessions();
void stop_sessions();
// Serializes admission with session close/revoke under the existing session
// owner lock. No token/status snapshot may authorize a later, stale start.
bool start_official_trace(uint32_t duration_ms,std::string &error);
bool start_heap_trace(uint32_t duration_ms,bool leaks_only,std::string &error);
bool start_gprof_trace(uint32_t duration_ms,std::string &error);
}
