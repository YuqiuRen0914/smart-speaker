#pragma once
#include <ArduinoJson.h>
#include <string>

bool device_api_maintenance_request(const std::string &state,
    const std::string &callback,const std::string &opener,JsonObject,std::string &error);
bool device_api_maintenance_exchange(const std::string &token,const std::string &state,
    JsonObject,std::string &error);
bool device_api_maintenance_session_authorized(const char *token);
void device_api_maintenance_session_revoke(const char *token);
const char *device_api_maintenance_readiness_error();
