#pragma once
#include <ArduinoJson.h>
#include <string>
namespace lunaredu::python::scripts {
inline constexpr const char *kScriptRoot = "/scripts";
inline constexpr const char *kAutorunPath = "/config/micropython-autorun.txt";
inline constexpr const char *kProjectAutorunPrefix = "@project/";
inline constexpr const char *kDefaultScriptPath = "/scripts/main.py";
// Called on the Python App's main executor; these operations allocate no VM.
bool write(const std::string &,const std::string &,bool,std::string &);
bool read(const std::string &,std::string &,std::string &);
bool remove(const std::string &,std::string &);
bool set_autorun(const std::string &,std::string &);
bool autorun(std::string &,std::string &);
bool set_project_autorun(const std::string &project_id,std::string &error);
// Transaction compensation for a selection returned by autorun().
bool restore_autorun_selection(const std::string &selection,std::string &error);
bool list(JsonObject,std::string &);
bool load_source(const std::string &,std::string &,std::string &,std::string &);
}
