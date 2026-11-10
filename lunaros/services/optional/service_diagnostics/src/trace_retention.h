#pragma once
#include "sdkconfig.h"
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::internal::trace_retention {
struct Status {
  bool configured=false, capturing=false, pending=false, truncated=false;
  size_t bytes=0, capacity=0;
  size_t staging_capacity=0, staged_bytes=0;
  uint32_t dropped_chunks=0;
  std::string last_result;
};
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD && (CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE || CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW)
bool begin(const char *kind,std::string &error,uint64_t deadline_ms=UINT64_MAX);
// Runtime transport path: bounded copy only; no allocator, filesystem or RTOS
// queue/semaphore API. The encoder serializes its writers; a critical section
// also protects the explicit start/stop boundary on the other CPU.
bool write(const void *bytes,size_t size);
void finish();
// Existing Main owner drains a bounded internal staging ring to PSRAM.
void drain();
bool saturated();
void collect();
bool shutdown(std::string &error);
Status status();
#else
inline bool begin(const char *,std::string &error,uint64_t=UINT64_MAX){error.clear();return true;}
inline bool write(const void *,size_t){return true;}
inline void finish(){}
inline void drain(){}
inline bool saturated(){return false;}
inline void collect(){}
inline bool shutdown(std::string &error){error.clear();return true;}
inline Status status(){return {};}
#endif
}
