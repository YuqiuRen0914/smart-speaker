#pragma once
#include "sdkconfig.h"
namespace lunaredu::lunaros::internal::coredump_tools {
#if CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_FLASH
void resume();void shutdown();void poll();
bool retire_acknowledged_capture(const char *source_sha256);
#else
inline void resume(){} inline void shutdown(){} inline void poll(){}
inline bool retire_acknowledged_capture(const char*){return true;}
#endif
}
