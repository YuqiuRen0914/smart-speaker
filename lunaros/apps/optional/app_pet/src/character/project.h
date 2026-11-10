#pragma once
#include <ArduinoJson.h>
#include "lunaredu_pet_catalog.h"
#include "character/expression_config.h"
#include <array>
#include <cstdint>
#include <string>

namespace lunaredu::pet::character {
inline constexpr size_t kMaximumProjectBytes=24*1024;
struct Project {
  std::string name;
  std::array<ExpressionConfig,kExpressionCount> expressions{};
  std::array<bool,kExpressionCount> overridden{};
};
bool parse_project(const std::string &,Project &,std::string &error);
bool install(const std::string &json,const std::string &sha256,std::string &error);
bool activate(const std::string &revision,std::string &error);
bool remove(std::string &error);
void restore();
void fill_state(JsonObject);
bool request_fetch(const std::string &url,const std::string &sha256,size_t bytes,std::string &error);
// Called only by the Network owner, never by the rendering owner.
void poll_fetch();
}
