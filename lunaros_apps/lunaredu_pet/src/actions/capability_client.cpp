#include "actions/capability_client.h"
#include "lunaredu_lunaros/app/app_capability_json.h"
#include <cstring>
namespace lunaredu::pet::actions {
constexpr const char *kCaller = "pet";
constexpr size_t kMaxToolInputBytes = 4096;

bool parse(const char *text, JsonDocument &document, std::string &error,
           const char *const *keys, size_t key_count) {
  if (!text || std::strlen(text) > kMaxToolInputBytes ||
      deserializeJson(document, text, DeserializationOption::NestingLimit(7)) != DeserializationError::Ok ||
      !document.is<JsonObject>()) { error = "invalid_application_tool_arguments"; return false; }
  for (JsonPairConst member : document.as<JsonObjectConst>()) {
    if (member.key().size() != std::strlen(member.key().c_str())) {
      error = "invalid_application_tool_argument_name"; return false;
    }
    bool known = false;
    for (size_t i = 0; i < key_count; ++i)
      known = known || std::strcmp(member.key().c_str(), keys[i]) == 0;
    if (!known) { error = "unknown_application_tool_argument"; return false; }
  }
  return true;
}
bool finish(bool ok, const JsonDocument &document, std::string &response, std::string &error) {
  response.clear();
  if (!ok) return false;
  if (document.overflowed() || measureJson(document) > 16384) {
    error = "application_tool_result_too_large"; return false;
  }
  serializeJson(document, response); error.clear(); return true;
}
bool identifier(JsonVariantConst value, size_t maximum) {
  if (!value.is<const char *>()) return false;
  const JsonString string = value.as<JsonString>();
  return string.size() && string.size() <= maximum && string.size() == std::strlen(string.c_str());
}
bool list(bool allow_changes, const char *arguments, std::string &response, std::string &error) {
  response.clear();
  static const char *const keys[] = {"provider", "offset", "limit"};
  JsonDocument input, output;
  if (!parse(arguments, input, error, keys, 3)) return false;
  const JsonObjectConst object = input.as<JsonObjectConst>();
  for (JsonPairConst member : object) {
    const bool is_provider = std::strcmp(member.key().c_str(), "provider") == 0;
    if (is_provider ? !identifier(member.value(), 64) : !member.value().is<uint32_t>()) {
      error = "invalid_capability_catalog_request"; return false;
    }
  }
  const bool ok = app::list_json_capabilities(kCaller, object["provider"] | "",
      object["offset"] | 0U, object["limit"] | 8U, output.to<JsonObject>(), error, allow_changes);
  return finish(ok, output, response, error);
}
bool describe(bool allow_changes, const char *arguments, std::string &response, std::string &error) {
  response.clear();
  static const char *const keys[] = {"id"};
  JsonDocument input, output;
  if (!parse(arguments, input, error, keys, 1)) return false;
  if (!identifier(input["id"], 96)) { error = "invalid_capability_id"; return false; }
  const bool ok = app::describe_json_capability(kCaller, input["id"].as<const char *>(),
                                               output.to<JsonObject>(), error, allow_changes);
  return finish(ok, output, response, error);
}
bool call(bool allow_changes, const char *arguments, std::string &response, std::string &error) {
  response.clear();
  static const char *const keys[] = {"id", "version", "arguments"};
  JsonDocument input, output;
  if (!parse(arguments, input, error, keys, 3)) return false;
  if (!identifier(input["id"], 96) || !input["version"].is<uint32_t>() ||
      !input["arguments"].is<JsonObject>()) {
    error = "invalid_capability_call"; return false;
  }
  output["id"] = input["id"];
  output["version"] = input["version"];
  const bool ok = app::call_json_capability(kCaller, input["id"].as<const char *>(),
      input["version"].as<uint32_t>(), input["arguments"].as<JsonObjectConst>(),
      output["result"].to<JsonObject>(), error, allow_changes);
  return finish(ok, output, response, error);
}
} // namespace lunaredu::pet::actions
