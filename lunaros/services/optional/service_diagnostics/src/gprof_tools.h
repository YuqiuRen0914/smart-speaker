#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include "sdkconfig.h"
namespace lunaredu::lunaros::internal::gprof_tools {
#if CONFIG_GPROF_ENABLE
void resume();
bool shutdown(std::string &error);
bool start(uint32_t duration_ms,uint64_t authorization_deadline,std::string &error);
void request_stop();
bool stop(std::string &error);
void poll(bool authorized);
// Serialize completed captures only on the existing diagnostic I/O job.
void collect();
void status(JsonObject output);
bool read(JsonObject output,std::string &error);
#else
inline void resume(){}inline void request_stop(){}inline void poll(bool){}inline void status(JsonObject){}
inline void collect(){}
inline bool shutdown(std::string &error){error.clear();return true;}
inline bool stop(std::string &error){error.clear();return true;}
inline bool start(uint32_t,uint64_t,std::string &error){error="gprof_not_configured";return false;}
inline bool read(JsonObject,std::string &error){error="gprof_not_configured";return false;}
#endif
}
