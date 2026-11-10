#include "device_api/http/device_web_routes.h"
#include "device_api/http/http_request_api.h"
#include "device_api/http/static_page.h"
#include "device_web/portal.h"
#include "lunaredu_lunaros/device_web/portal.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include <cassert>
#include <cstring>
#include <map>
#include <iostream>
namespace {
bool ap = false, available = true, frozen = false;
StaticPageOptions root;
std::map<std::string, HttpSimpleHandler> routes;
std::string body;
JsonDocument config;
}
bool device_api_provisioning_surface_active() { return ap; }
void setup_static_page(const StaticPageOptions &options) { assert(frozen); root = options; }
bool register_http_route(const char *uri, HTTPMethod method, HttpSimpleHandler handler) {
  assert(method == HTTP_GET); assert(routes.emplace(uri, handler).second); return true;
}
void add_response_header(const char *name, const char *value) {
  assert(std::strcmp(name,"Cache-Control") == 0 && std::strcmp(value,"no-store") == 0);
}
bool send_http_binary_response(int status, const char *type, const uint8_t *data, size_t size) {
  assert(status == 200 && std::strcmp(type,"text/html; charset=utf-8") == 0);
  body.assign(reinterpret_cast<const char *>(data), size); return true;
}
void serialize_and_send_json(int status, JsonDocument &doc) { assert(status == 200); config.set(doc); }
namespace lunaredu::lunaros {
bool service_available(const char *) { return available; }
const DeviceOperationProjection *find_device_operation_projection(const char *, const char *operation) {
  static const DeviceOperationProjection projection{};
  return available && std::strcmp(operation,"firmware.update") != 0 ? &projection : nullptr;
}
namespace device_web {
void freeze() { frozen = true; }
const char *product_name() { return "Test <product>"; }
PageView page(Page selected, bool recovery) {
  const char *pages[] = {"home","network","binding","maintenance"};
  const char *text = recovery ? "builtin" : pages[static_cast<size_t>(selected)];
  return {reinterpret_cast<const uint8_t *>(text),std::strlen(text)};
}
}
}
int main() {
  setup_device_web_routes();
#if CONFIG_LUNAREDU_DEVICE_WEB
  assert(routes.size() == 5 && root.read_page_view && root.redirect_not_found);
  assert(std::strcmp(root.not_found_redirect_location,"http://192.168.4.1/setup") == 0);
  StaticPageView view;
  for (bool provisioning : {false,true}) {
    ap = provisioning;
    assert(root.read_page_view(view));
    assert(std::string(reinterpret_cast<const char *>(view.data),view.size) == (ap ? "network" : "home"));
    assert(root.redirect_not_found() == ap);
    routes.at("/setup")(); assert(body == "network");
    routes.at("/binding")(); assert(body == "binding");
    routes.at("/maintenance")(); assert(body == "maintenance");
    routes.at("/recovery")(); assert(body == "builtin");
    routes.at("/device-web/config")();
    assert(config["provisioning"].as<bool>() == ap);
    assert(config["features"]["identity"].as<bool>());
    assert(!config["features"]["cloud_update"].as<bool>());
    assert(config["binding_ready"].as<bool>());
  }
  available = false; routes.at("/device-web/config")();
  assert(!config["features"]["binding"].as<bool>() && !config["binding_ready"].as<bool>());
#else
  assert(routes.empty() && !root.read_page_view && !frozen);
  std::string error;
  assert(!lunaredu::lunaros::configure_device_web({},error) && error == "device_web_disabled");
#endif
  std::cout << "Device Web HTTP routes passed (enabled=" << CONFIG_LUNAREDU_DEVICE_WEB << ")\n";
}
