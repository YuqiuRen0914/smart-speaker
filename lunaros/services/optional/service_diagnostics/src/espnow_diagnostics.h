#pragma once
#include "sdkconfig.h"
namespace lunaredu::lunaros::internal {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
bool start_espnow_diagnostics();
void stop_espnow_diagnostics();
void poll_espnow_diagnostics();
#else
inline bool start_espnow_diagnostics(){return true;}
inline void stop_espnow_diagnostics(){}
inline void poll_espnow_diagnostics(){}
#endif
}
