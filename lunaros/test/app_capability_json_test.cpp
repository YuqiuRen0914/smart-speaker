#include "lunaredu_lunaros/app/app_capability_json.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace lunaredu::app;
namespace {
bool available = true, bad_output = false;
int calls = 0, unrelated_probes = 0;
bool readiness(void *, std::string &reason) { reason = "fixture_offline"; return available; }
bool invoke(void *, const char *caller, JsonObjectConst, JsonObject result, std::string &) {
  assert(std::string(caller) == "pet"); ++calls;
  if (bad_output) result["done"] = "true";
  else result["done"] = true;
  return true;
}
const char *const choices[] = {"on", "off"};
const char *const callers[] = {"pet"};
const char *const private_callers[] = {"product"};
const CapabilityField input[] = {
    {"enabled", CapabilityFieldType::Boolean},
    {"duration", CapabilityFieldType::Integer, true, "Milliseconds", 1, 100},
    {"speed", CapabilityFieldType::Number, false, "Normalised", -1, 1},
    {"mode", CapabilityFieldType::String, false, "", 0, 0, 2, 3, choices, 2},
    {"note", CapabilityFieldType::String, false, "Two Unicode characters", 0, 0, 2, 2},
};
const CapabilityField output[] = {{"done", CapabilityFieldType::Boolean}};
const JsonCapabilityDefinition definition{
    "fixture.run", "fixture", 1, "Fixture", "Test a bounded command",
    CapabilityExecution::Command, {input, 5}, {output, 1}, invoke, readiness, nullptr, callers, 1};
const JsonCapabilityDefinition hidden{
    "fixture.private", "fixture", 1, "Private", "Not visible to Pet",
    CapabilityExecution::Query, {}, {output, 1}, invoke, nullptr, nullptr, private_callers, 1};
}

int main() {
  std::string error;
  assert(register_json_capability(definition, error));
  assert(register_json_capability(definition, error));
  assert(register_json_capability(hidden, error));
  auto conflict = definition; conflict.title = "Changed";
  assert(!register_json_capability(conflict, error));
  auto invalid = definition; invalid.version = 0;
  assert(!register_json_capability(invalid, error));
  const CapabilityField invalid_fields[] = {{"same"}, {"same"}};
  invalid = definition; invalid.input = {invalid_fields, 2};
  assert(!register_json_capability(invalid, error));

  assert(register_capability({"native.only", "native", 42, sizeof(int), sizeof(int),
      [](void *, const CapabilityCall &, std::string &) { return true; },
      [](void *) { ++unrelated_probes; return true; }, nullptr, callers, 1}, error));

  JsonDocument result, arguments;
  assert(list_json_capabilities("pet", nullptr, 0, 8, result.to<JsonObject>(), error));
  assert(result["total"] == 1);
  assert(result["capabilities"][0]["id"] == "fixture.run");
  assert(unrelated_probes == 0);
  assert(!describe_json_capability("pet", "fixture.private", result.to<JsonObject>(), error));
  assert(!describe_json_capability("unknown", "fixture.run", result.to<JsonObject>(), error));
  assert(!describe_json_capability("pet", "native.only", result.to<JsonObject>(), error));
  assert(describe_json_capability("pet", "fixture.run", result.to<JsonObject>(), error));
  assert(result["input_schema"]["additionalProperties"] == false);
  assert(result["input_schema"]["properties"]["duration"]["maximum"] == 100);
  assert(result["input_schema"]["properties"]["mode"]["enum"].size() == 2);

  auto call = [&](const char *json, bool changes = true, uint32_t version = 1) {
    assert(deserializeJson(arguments, json) == DeserializationError::Ok);
    return call_json_capability("pet", "fixture.run", version,
        arguments.as<JsonObjectConst>(), result.to<JsonObject>(), error, changes);
  };
  assert(call("{\"enabled\":true,\"duration\":10,\"speed\":0.5,\"mode\":\"on\",\"note\":\"明月\"}"));
  assert(result["done"] == true && calls == 1);
  const char *rejected[] = {
      "{}", "{\"enabled\":\"true\",\"duration\":10}",
      "{\"enabled\":1,\"duration\":10}", "{\"enabled\":true,\"duration\":false}",
      "{\"enabled\":true,\"duration\":10.5}", "{\"enabled\":true,\"duration\":101}",
      "{\"enabled\":true,\"duration\":10,\"speed\":2}",
      "{\"enabled\":true,\"duration\":10,\"mode\":\"yes\"}",
      "{\"enabled\":true,\"duration\":10,\"extra\":1}",
      "{\"enabled\":true,\"duration\":10,\"note\":\"明月呀\"}",
      "{\"enabled\\u0000extra\":true,\"duration\":10}",
      "{\"enabled\":true,\"duration\":10,\"mode\":\"on\\u0000off\"}", "[]", "null",
  };
  for (const char *json : rejected) { assert(!call(json)); assert(result.size() == 0); }
  assert(!call("{\"enabled\":true,\"duration\":10}", false));
  assert(!call("{\"enabled\":true,\"duration\":10}", true, 2));
  assert(calls == 1);
  available = false;
  assert(describe_json_capability("pet", "fixture.run", result.to<JsonObject>(), error));
  assert(result["available"] == false && result["unavailable_reason"] == "fixture_offline");
  assert(!call("{\"enabled\":true,\"duration\":10}"));
  assert(calls == 1);
  available = true; bad_output = true;
  assert(!call("{\"enabled\":true,\"duration\":10}"));
  assert(error.find("capability_result_contract_violation") == 0 && result.size() == 0);
  assert(unrelated_probes == 0);

  // A later provider is discovered without restarting a caller or caching tools.
  static JsonCapabilityDefinition pages[10];
  static std::string ids[10];
  for (int i = 0; i < 10; ++i) {
    ids[i] = "pages." + std::to_string(i); pages[i] = definition;
    pages[i].id = ids[i].c_str(); pages[i].provider_app = "pages";
    assert(register_json_capability(pages[i], error));
  }
  assert(list_json_capabilities("pet", "pages", 0, 8, result.to<JsonObject>(), error));
  assert(result["capabilities"].size() == 8 && result["total"] == 10 && result["next_offset"] == 8);
  assert(list_json_capabilities("pet", "pages", 8, 8, result.to<JsonObject>(), error));
  assert(result["capabilities"].size() == 2 && result["next_offset"].isNull());
  assert(!list_json_capabilities("pet", nullptr, 0, 9, result.to<JsonObject>(), error));

  static const CapabilityField nested_fields[]={{"enabled",CapabilityFieldType::Boolean}};
  static const CapabilitySchema nested_schema{nested_fields,1};
  static const auto nested_item=object_field("item",nested_schema);
  static const CapabilityField nested_list[]={array_field("items",nested_item,2)};
  static const JsonCapabilityDefinition nested{"nested.echo","nested",1,"Nested","Closed nested arrays",CapabilityExecution::Command,
    {nested_list,1},{nested_list,1},[](void *,const char *,JsonObjectConst input,JsonObject output,std::string &){
      ++calls; output.set(input); if(bad_output) output["items"][0]["enabled"]="bad"; return true;
    },nullptr,nullptr,callers,1};
  assert(register_json_capability(nested,error));
  assert(describe_json_capability("pet","nested.echo",result.to<JsonObject>(),error));
  assert(result["input_schema"]["properties"]["items"]["maxItems"]==2);
  assert(result["input_schema"]["properties"]["items"]["items"]["additionalProperties"]==false);
  auto nested_call=[&](const char *json){
    assert(deserializeJson(arguments,json)==DeserializationError::Ok);
    return call_json_capability("pet","nested.echo",1,arguments.as<JsonObjectConst>(),result.to<JsonObject>(),error);
  };
  bad_output=false;
  assert(nested_call(R"({"items":[{"enabled":true}]})"));
  const auto before=calls;
  for(const char *json:{R"({"items":[{"enabled":"true"}]})",R"({"items":[{"enabled":true,"extra":1}]})",
      R"({"items":[{},{}]})",R"({"items":[true]})",R"({"items":null})",R"({"items":[{"enabled":true},{"enabled":false},{"enabled":true}]})"})
    assert(!nested_call(json));
  assert(calls==before);
  bad_output=true;
  assert(!nested_call(R"({"items":[{"enabled":true}]})"));
  assert(error.find("capability_result_contract_violation")==0);
  CapabilityField cyclic{"cycle",CapabilityFieldType::Object};
  CapabilitySchema cycle{&cyclic,1}; cyclic.object_schema=&cycle;
  invalid=nested; invalid.id="invalid.cycle"; invalid.input=cycle;
  assert(!register_json_capability(invalid,error));
  std::cout << "OS capability schema, permissions, late discovery and fault paths passed\n";
}
