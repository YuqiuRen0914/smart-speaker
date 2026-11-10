#include "lunaredu_pet_application_tools.h"
#include "lunaredu_pet_conversation.h"
#include "lunaredu_weather_information_service.h"
#include "lunaredu_smart_plug_service.h"
#include "lunaredu_smart_plug.h"
#include "lunaredu_lunaros/app/app_capability_json.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include <cassert>
#include <iostream>
#include <map>

namespace {
std::map<std::string, lunaredu::pet::conversation::ToolRegistration> tools;
lunaredu::information::WeatherSnapshot weather;
lunaredu::smart_plug::SmartPlugStatus plug;
bool allow_control = true;
bool on_owner = false;
int relay_calls = 0;
}
// Only transport registration, OS task scheduling and external data/hardware
// are replaced. The Pet bridge, registry, schemas, app adapters, service start
// contributions and Smart Plug owner dispatch are the production sources.
namespace lunaredu::pet::conversation {
bool register_tool(const ToolRegistration &tool) { tools[tool.name] = tool; return true; }
}
namespace lunaredu::information {
bool setup_information_runtime(const RuntimeConfig &) { weather.initialized = true; return true; }
WeatherSnapshot weather_snapshot() { return weather; }
}
namespace lunaredu::lunaros {
bool invoke_service_operation(const char *owner, ServiceOperation operation, void *context,
                              std::string &error, uint32_t) {
  assert(std::string(owner) == kMainServiceExecutor);
  ServiceOperationResult result;
  on_owner = true; operation(context, result); on_owner = false;
  error = result.error; return result.ok;
}
}
namespace lunaredu::smart_plug {
SmartPlugConfig smart_plug_config_from_kconfig() { return {}; }
bool setup_smart_plug(const SmartPlugConfig &) { plug.initialized = true; return true; }
void smart_plug_deinit() { plug.initialized = false; }
void tick_smart_plug() {}
SmartPlugStatus smart_plug_status() { return plug; }
bool smart_plug_available() { return plug.initialized; }
bool smart_plug_set_relay(bool enabled, std::string &error) {
  assert(on_owner); ++relay_calls;
  if (plug.fault_latched && enabled) { error = "protection_fault"; return false; }
  plug.relay_on = enabled; return true;
}
bool smart_plug_toggle_relay(std::string &) { assert(false); return false; }
bool smart_plug_clear_energy(std::string &) { assert(false); return false; }
bool smart_plug_clear_fault(std::string &) { assert(false); return false; }
bool smart_plug_apply_config_update(const SmartPlugRuntimeConfigUpdate &, std::string &) { assert(false); return false; }
}

int main() {
  using namespace lunaredu;
  std::string error, response;
  JsonDocument document;
  assert(pet::register_application_tools({nullptr, [](void *) { return allow_control; }}, error));
  assert(tools.size() == 3);
  auto run = [&](const char *name, const char *arguments) {
    auto &tool = tools.at(name);
    const bool ok = tool.handler(tool.context, arguments, response, error);
    document.clear();
    if (ok) assert(deserializeJson(document, response) == DeserializationError::Ok);
    else assert(response.empty());
    return ok;
  };
  assert(run("apps.list", "{}") && document["total"] == 0);
  information::RuntimeConfig config;
  auto service = information::runtime_service_node(config, nullptr, 0);
  assert(service.start(service.context).disposition == lunaros::ServiceStartDisposition::Ready);
  auto socket = smart_plug::make_service();
  assert(socket.start(socket.context).disposition == lunaros::ServiceStartDisposition::Ready);
  assert(run("apps.list", "{}") && document["total"] == 3);
  assert(run("apps.describe", "{\"id\":\"weather.current.get\"}"));
  assert(document["available"] == false);
  weather.available = true; weather.location = "重庆"; weather.temperature_c = 28;
  weather.updated_epoch = 1788750000; weather.stale = true;
  allow_control = false;
  assert(run("apps.describe", "{\"id\":\"smart_plug.power.set\"}"));
  assert(document["available"] == false && document["unavailable_reason"] == "application_capability_changes_blocked");
  assert(run("apps.call", "{\"id\":\"weather.current.get\",\"version\":1,\"arguments\":{}}"));
  assert(document["result"]["location"] == "重庆" && document["result"]["stale"] == true);
  assert(document["result"]["temperature_c"] == 28);
  assert(!run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"arguments\":{\"enabled\":true}}"));
  assert(relay_calls == 0);
  allow_control = true;
  assert(!run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"arguments\":{\"enabled\":\"true\"}}"));
  assert(!run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":2,\"arguments\":{\"enabled\":true}}"));
  assert(!run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"caller\":\"product\",\"arguments\":{\"enabled\":true}}"));
  assert(relay_calls == 0);
  assert(run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"arguments\":{\"enabled\":true}}"));
  assert(plug.relay_on && document["result"]["enabled"] == true && relay_calls == 1);
  plug.fault_latched = true;
  assert(!run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"arguments\":{\"enabled\":true}}"));
  assert(error == "protection_fault");
  assert(run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"arguments\":{\"enabled\":false}}"));
  assert(!plug.relay_on);
  assert(socket.stop(socket.context, error));
  assert(run("apps.describe", "{\"id\":\"smart_plug.power.set\"}") && document["available"] == false);
  assert(!run("apps.call", "{\"id\":\"smart_plug.power.set\",\"version\":1,\"arguments\":{\"enabled\":true}}"));
  assert(!run("apps.call", "{\"id\":\"weather.current.get\",\"version\":1,\"arguments\":{\"city\":\"北京\"}}"));
  assert(!run("apps.list", "{\"limit\":9}"));
  assert(!run("apps.describe", "[]"));
  assert(!run("apps.describe", "{\"id\":\"weather.current.get\\u0000fake\"}"));
  std::cout << "Pet -> OS -> weather/Smart Plug discovery, schema, policy and owner dispatch passed\n";
}
