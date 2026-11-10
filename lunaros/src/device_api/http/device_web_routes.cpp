#include "device_api/http/device_web_routes.h"
#include "device_api/http/http_request_api.h"
#include "device_api/http/static_page.h"
#include "device_api/access/device_api_request_guard.h"
#include "device_web/portal.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/kernel/service_registry.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "sdkconfig.h"
#if CONFIG_LUNAREDU_DEVICE_WEB
namespace {
using namespace lunaredu::lunaros;
using device_web::Page;
bool read_root(StaticPageView &view) {
  const auto content = device_web::page(device_api_provisioning_surface_active() ? Page::Network : Page::Home);
  view = {content.data, content.size}; return view.size != 0;
}
void serve(Page selected, bool recovery = false) {
  const auto view = device_web::page(selected, recovery);
  add_response_header("Cache-Control", "no-store");
  send_http_binary_response(200, "text/html; charset=utf-8", view.data, view.size);
}
void config() {
  JsonDocument doc;
  doc["product_name"] = device_web::product_name();
  doc["provisioning"] = device_api_provisioning_surface_active();
  auto features = doc["features"].to<JsonObject>();
  features["identity"] = find_device_operation_projection("identity", "name.set") != nullptr;
  features["system"] = find_device_operation_projection("system", "info.get") != nullptr;
  features["network"] = find_device_operation_projection("network", "provision.submit") != nullptr;
  features["binding"] = find_device_operation_projection("device_link", "pairing.start") != nullptr;
  features["ota"] = find_device_operation_projection("ota", "firmware.upload") != nullptr;
  features["cloud_update"] = find_device_operation_projection("device_link", "firmware.update") != nullptr;
  doc["binding_ready"] = service_available(kDeviceLinkServiceId);
  doc["ota_ready"] = service_available(kUpdateServiceId);
  add_response_header("Cache-Control", "no-store");
  serialize_and_send_json(200, doc);
}
}
#endif
void setup_device_web_routes() {
#if CONFIG_LUNAREDU_DEVICE_WEB
  device_web::freeze();
  StaticPageOptions options;
  options.route = "/";
  options.not_found_redirect_location = "http://192.168.4.1/setup";
  options.read_page_view = read_root;
  options.redirect_not_found = device_api_provisioning_surface_active;
  setup_static_page(options);
  register_http_route("/setup", HTTP_GET, [] { serve(Page::Network); });
  register_http_route("/binding", HTTP_GET, [] { serve(Page::Binding); });
  register_http_route("/maintenance", HTTP_GET, [] { serve(Page::Maintenance); });
  register_http_route("/recovery", HTTP_GET, [] { serve(Page::Home, true); });
  register_http_route("/device-web/config", HTTP_GET, config);
#endif
}
