#pragma once
#include <cstdint>
#include <string>
#include <ArduinoJson.h>
#include "sdkconfig.h"
namespace lunaredu::lunaros::internal::heap_tools {
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
void resume();
bool shutdown(std::string &error);
bool start(uint32_t duration_ms, bool leaks_only, uint64_t authorization_deadline_ms, std::string &error);
bool stop(std::string &error);
void poll(bool session_active);
void status(JsonObject output);
bool read(JsonObject output, std::string &error);
bool upload_authority_for_generation(uint32_t generation,int &epoch,uint64_t &credential);
#else
inline void resume(){}inline void poll(bool){}inline void status(JsonObject){}
inline bool shutdown(std::string &error){error.clear();return true;}
inline bool stop(std::string &error){error.clear();return true;}
inline bool start(uint32_t,bool,uint64_t,std::string &error){error="heap_trace_not_configured";return false;}
inline bool read(JsonObject,std::string &error){error="heap_trace_not_configured";return false;}
inline bool upload_authority_for_generation(uint32_t,int&,uint64_t&){return false;}
#endif
}
