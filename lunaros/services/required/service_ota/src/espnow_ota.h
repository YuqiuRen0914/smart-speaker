#pragma once
#include "sdkconfig.h"
#include <string>
namespace lunaredu::lunaros::internal {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
bool bind_espnow_ota();
void poll_espnow_ota();
bool espnow_ota_admitted();
bool stop_espnow_ota(std::string &error);
#else
inline bool bind_espnow_ota(){return true;}
inline void poll_espnow_ota(){}
inline bool espnow_ota_admitted(){return false;}
inline bool stop_espnow_ota(std::string &error){error.clear();return true;}
#endif
}
