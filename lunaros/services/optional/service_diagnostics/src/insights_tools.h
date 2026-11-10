#pragma once
#include "sdkconfig.h"
namespace lunaredu::lunaros::internal::insights_tools {
inline const char *metric_profile(){
#if !CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS
  return "disabled";
#elif CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS_IO
  return "io";
#else
  return "system";
#endif
}
#if CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS
void resume();void poll();void shutdown();
#else
inline void resume(){} inline void poll(){} inline void shutdown(){}
#endif
}
