#include "lunaredu_pet_application_tools.h"
#include "lunaredu_pet_conversation.h"
#include "actions/capability_client.h"
#include <mutex>

namespace lunaredu::pet {
namespace {
ApplicationToolPolicy policy;
std::mutex policy_mutex;
bool changes_allowed() {
  ApplicationToolPolicy current;
  {
    std::lock_guard<std::mutex> lock(policy_mutex);
    current = policy;
  }
  return !current.control_allowed || current.control_allowed(current.context);
}
bool list(void *, const char *arguments, std::string &response, std::string &error) {
  return actions::list(changes_allowed(), arguments, response, error);
}
bool describe(void *, const char *arguments, std::string &response, std::string &error) {
  return actions::describe(changes_allowed(), arguments, response, error);
}
bool call(void *, const char *arguments, std::string &response, std::string &error) {
  return actions::call(changes_allowed(), arguments, response, error);
}
constexpr const char *kListSchema = R"json({"type":"object","additionalProperties":false,"properties":{"provider":{"type":"string","minLength":1,"maxLength":64},"offset":{"type":"integer","minimum":0,"maximum":64},"limit":{"type":"integer","minimum":1,"maximum":8}}})json";
constexpr const char *kDescribeSchema = R"json({"type":"object","additionalProperties":false,"properties":{"id":{"type":"string","minLength":1,"maxLength":96}},"required":["id"]})json";
constexpr const char *kCallSchema = R"json({"type":"object","additionalProperties":false,"properties":{"id":{"type":"string","minLength":1,"maxLength":96},"version":{"type":"integer","minimum":1},"arguments":{"type":"object","description":"Use the exact input_schema returned by apps.describe."}},"required":["id","version","arguments"]})json";
}

bool register_application_tools(const ApplicationToolPolicy &value, std::string &error) {
  {
    std::lock_guard<std::mutex> lock(policy_mutex);
    policy = value;
  }
  const conversation::ToolRegistration tools[] = {
      {"apps.list", "查询 OS 中当前 Pet 可调用的应用能力，可按 provider 筛选并翻页。", kListSchema, list, nullptr},
      {"apps.describe", "获取能力的版本、输入输出结构、执行类型和当前可用原因。调用前先查看参数。", kDescribeSchema, describe, nullptr},
      {"apps.call", "通过 OS 执行已声明的能力。按返回结果反馈；activity 表示持续任务，不代表完成。", kCallSchema, call, nullptr},
  };
  for (const auto &tool : tools) {
    if (!conversation::register_tool(tool)) { error = "application_tool_registration_failed:" + tool.name; return false; }
  }
  error.clear(); return true;
}
}
