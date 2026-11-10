#include "lunaros_platform_link_service_id.h"
#include "lunaros_update_service_id.h"
#include "app/lifecycle/app_controller_view.h"
#include "device_api/adapters/application_projection.h"
#include "device_api/routes/device_api_routes.h"
#include "device_api/http/http_request_api.h"
#include "device_api/http/static_page.h"
#include "device_api/access/device_api_request_guard.h"
#include "device_api/access/device_api_bound_access.h"
#include "device_api/access/device_api_maintenance_access.h"
#include "device_web/portal_internal.h"
#include "device_api/device_projection.h"
#include "device_api/device_api_context.h"
#include "kernel/runtime.h"
#include "lunaros_platform_link.h"
#include "lunaros_network.h"
#include "system/identity.h"
#include "configuration/lifecycle.h"
#include "configuration/device_config.h"
#include "lunaredu_ota_recovery.h"
#include "sdkconfig.h"
#include <cstdlib>
#include <algorithm>
#if CONFIG_LUNAREDU_DEVICE_WEB
namespace {
using namespace lunaredu::lunaros;
using device_web::Page;
bool read_root(StaticPageView &view) {
  const auto content = device_web::page(Page::Network, true);
  view = {content.data, content.size, content.gzip}; return view.size != 0;
}
void serve(Page selected, bool recovery = false) {
  const auto view = device_web::page(selected, recovery);
  add_response_header("Cache-Control", "no-store");
  if (view.gzip) add_response_header("Content-Encoding", "gzip");
  send_http_binary_response(200, "text/html; charset=utf-8", view.data, view.size);
}
void serve_asset(device_web::DeviceWebAsset asset, const char *content_type) {
  const auto view = device_web::asset(asset);
  add_response_header("Cache-Control", "no-store");
  if (view.gzip) add_response_header("Content-Encoding", "gzip");
  send_http_binary_response(200, content_type, view.data, view.size);
}
void config(DeviceProjectionRegistry &registry) {
  auto doc=make_http_json_document();
  doc["passport_authorization_url"] = kDevicePassportAuthorizationUrl;
  doc["administrator_authentication_protocol"] = 2;
  doc["serial"] = hardware_serial_number();
  doc["device_uuid"] = device_identity_config().local_device_uuid;
  if (device_web_bound_static_page_allowed("/")) {
    const std::string browser_origin = platform_link_binding_browser_origin();
    if (!browser_origin.empty()) doc["bound_board_url"] = browser_origin + "/link";
  }
  doc["product_name"] = device_web::product_name();
  doc["provisioning"] = device_api_provisioning_surface_active();
  const bool access_required=device_api_bound_access_required();
  doc["access_required"] = access_required;
  const bool authority_ready=platform_link_local_authority_ready();
  const auto pairing=platform_link_pairing_snapshot();
  const bool confirmed_bound=authority_ready &&
      (pairing.state==PlatformLinkBindingState::Bound || !pairing.account_id.empty());
  doc["bound"] = confirmed_bound;
  doc["local_authority_ready"] = authority_ready;
  doc["binding_authority"] = confirmed_bound ? "bound" : authority_ready ? "unbound" : "unknown";
  doc["credential_format_ready"] = device_cloud_credential().size()>=43;
  const bool administrator = device_api_admin_access_authorized(
      request_header("X-Lunar-Debug-Token").c_str());
  doc["administrator_authorized"] = administrator;
  const char *admin_error = device_api_admin_readiness_error();
  doc["administrator_authentication_ready"] = admin_error[0] == '\0';
  doc["administrator_authentication_error"] = admin_error;
  doc["configuration_recovery_active"] = configuration_recovery_active();
  doc["configuration_recovery_snapshot_available"] = configuration_recovery_snapshot_available();
  doc["management_authorized"] = administrator || device_api_bound_access_authorized(
      request_header("X-Lunar-Access-Grant").c_str());
  // Public diagnostics contain fixed categories/status only. Network names,
  // addresses, account details, pairing codes and raw errors stay in API.
  const auto network = network_snapshot();
  auto summary = doc["summary"].to<JsonObject>();
  summary["network_connected"] = network.link == NetworkLinkState::Up;
  summary["bluetooth_available"] = network.bluetooth_available;
  summary["bluetooth_enabled"] = network.bluetooth_enabled;
  summary["bluetooth_connected"] = network.bluetooth_connected;
  const auto platform = platform_link_platform_connection_snapshot();
  summary["platform_state"] = platform.state == PlatformLinkPlatformConnectionState::Connected
      ? "connected" : platform.state == PlatformLinkPlatformConnectionState::Error ? "error" : "disconnected";
  summary["platform_http_status"] = platform.status_code;
  summary["platform_operation"] = platform.operation;
  summary["platform_error_code"] = platform.error_code;
  summary["version"] = firmware_version();
  doc["application_name"] = device_web::application_name();
  auto overrides = doc["custom_pages"].to<JsonArray>();
  const char *page_names[] = {"application", "network", "binding", "maintenance"};
  for (size_t i = 0; i < 4; ++i)
    if (device_web::has_page_override(static_cast<Page>(i))) overrides.add(page_names[i]);
  auto features = doc["features"].to<JsonObject>();
  bool controls = false;
  registry.visit_modules([](const DeviceModuleProjection &module, void *opaque) {
    if (module.module && module.module->kind &&
        std::string(module.module->kind) == "boolean") *static_cast<bool *>(opaque) = true;
  }, &controls);
  features["controls"] = controls;
  features["application_page"] = device_web::has_page_override(Page::Application);
  features["applications"] = registry.find_operation("app", "list") != nullptr &&
      device_api_has_applications(registry.device());
  const bool factory_page = device_web_bound_factory_page_allowed("/factory");
  features["factory_page"] = factory_page;
  features["factory_mode"] = factory_page &&
      registry.find_operation("factory_mode", "status.get") != nullptr &&
      registry.find_operation("factory_mode", "catalog.get") != nullptr &&
      registry.find_operation("factory_mode", "start") != nullptr;
  features["factory_test"] =
      registry.find_operation("bionic_hand", "state.get") != nullptr &&
      registry.find_operation("bionic_hand", "gesture.set") != nullptr &&
      registry.find_operation("bionic_hand", "finger.set") != nullptr &&
      registry.find_operation("bionic_hand", "motion.stop") != nullptr &&
      registry.find_operation("bionic_hand", "output.off") != nullptr;
  features["factory_audio"] = factory_page &&
      registry.find_operation("factory_audio", "status.get") != nullptr &&
      registry.find_operation("factory_audio", "speaker.start") != nullptr &&
      registry.find_operation("factory_audio", "microphone.start") != nullptr;
  features["factory_voice"] = factory_page &&
      registry.find_operation("assistant", "status.get") != nullptr &&
      registry.find_operation("assistant", "listening.toggle") != nullptr;
  features["factory_media"] = factory_page &&
      registry.find_operation("media", "status.get") != nullptr;
  features["identity"] = registry.find_operation("identity", "name.set") != nullptr;
  features["desktop_settings"] = registry.find_operation("desktop_settings", "preferences.get") != nullptr;
  features["system"] = registry.find_operation("system", "info.get") != nullptr;
  features["debug"] = registry.find_operation("system", "diagnostics.get") != nullptr;
  features["debug_logs"] = registry.find_operation("debug", "logs.read") != nullptr;
  features["screen_capture"] = registry.find_operation("screen", "frame.capture") != nullptr;
  features["service_recovery"] = registry.find_operation("service", "restart") != nullptr;
  features["network"] = registry.find_operation("network", "wifi.connect") != nullptr;
  features["provisioning"] = registry.find_operation("provisioning", "submit") != nullptr;
  features["bluetooth_control"] = registry.find_operation("network", "bluetooth.set") != nullptr;
  features["network_config"] = registry.find_operation("network", "config.get") != nullptr &&
      registry.find_operation("network", "config.set") != nullptr;
  features["network_tls"] = registry.find_operation("network", "tls.get") != nullptr &&
      registry.find_operation("network", "tls.set") != nullptr;
  features["binding"] = registry.find_operation("platform_link", "pairing.start") != nullptr;
  features["binding_unbind"] = registry.find_operation("platform_link", "binding.unbind") != nullptr;
  doc["binding_unbind_allowed"] = device_web_operation_permitted("platform_link.binding.unbind");
  features["environment"] = !platform_link_uses_remote_base_url() &&
      registry.find_operation("platform_link", "environment.set") != nullptr;
  features["assistant_endpoint"] =
      registry.find_operation("assistant_endpoint", "status.get") != nullptr &&
      registry.find_operation("assistant_endpoint", "base_url.set") != nullptr;
  features["assistant_settings"] = registry.find_operation("assistant", "preferences.get") != nullptr &&
      registry.find_operation("assistant", "enabled.set") != nullptr &&
      registry.find_operation("assistant", "microphone.enabled.set") != nullptr &&
      registry.find_operation("assistant", "standby_mode.set") != nullptr &&
      registry.find_operation("assistant", "volume.set") != nullptr;
  features["assistant_preferences"] = registry.find_operation("assistant_preferences", "preferences.get") != nullptr;
  features["remote_receiver"] = registry.find_operation("remote_receiver", "status.get") != nullptr &&
      registry.find_operation("remote_receiver", "enabled.set") != nullptr &&
      registry.find_operation("remote_receiver", "pairing.begin") != nullptr &&
      registry.find_operation("remote_receiver", "pairing.clear") != nullptr;
  doc["environment_allowed"] = device_web_operation_permitted("platform_link.environment.set") &&
      device_web_operation_permitted("platform_link.environment.restart");
  features["ota"] = registry.find_operation("ota", "firmware.upload") != nullptr;
  features["cloud_update"] = registry.find_operation("platform_link", "firmware.check") != nullptr &&
      registry.find_operation("platform_link", "firmware.update") != nullptr &&
      device_web_operation_permitted("platform_link.firmware.check") &&
      device_web_operation_permitted("platform_link.firmware.update");
  doc["binding_ready"] = registry.runtime().service_available(kPlatformLinkServiceId);
  doc["ota_ready"] = registry.runtime().service_available(kUpdateServiceId);
  add_response_header("Cache-Control", "no-store");
  serialize_and_send_json(200, doc);
}

void admin_request() {
  add_response_header("Cache-Control", "no-store");
  add_response_header("Referrer-Policy", "no-referrer");
  const std::string state = request_arg("state");
  if (state.size()!=64 || !std::all_of(state.begin(),state.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})) {
    send_json_response(400,R"({"ok":false,"error":{"code":"maintenance_request_invalid"}})");return;
  }
  const std::string callback = request_arg("callback_origin");
  const std::string host = request_header("Host");
  const size_t port = host.find(':');
  const std::string ip = host.substr(0, port);
  const auto network = network_snapshot();
  // Never attest an arbitrary browser-supplied destination or a rebinding DNS
  // name. The local IPv4 address is taken from the device's network service.
  bool valid_host = !ip.empty() && ((ip == network.local_ip && ip != "0.0.0.0") ||
      (ip == network.local_access_ip && network.softap_active));
  if (port != std::string::npos) {
    const std::string number = host.substr(port + 1);
    valid_host = valid_host && !number.empty() && number.size() <= 5 &&
        number.find_first_not_of("0123456789") == std::string::npos &&
        std::strtoul(number.c_str(), nullptr, 10) > 0 &&
        std::strtoul(number.c_str(), nullptr, 10) <= 65535;
  }
  if (!valid_host || callback != "http://" + host) {
    send_json_response(400, R"({"error":"请使用设备的局域网 IP 地址打开网页后重试认证。"})"); return;
  }
  const std::string origin=request_header("Origin");
  const std::string board=platform_link_binding_browser_origin();
  if(!origin.empty() && origin!=callback && !device_api_origin_allowed(origin.c_str(),network.local_ip.c_str(),board.c_str())) {
    send_json_response(403,R"({"ok":false,"error":{"code":"origin_not_allowed"}})");return;
  }
  auto doc=make_http_json_document();std::string error;
  const bool issued=device_api_maintenance_request(state,callback,origin.empty()?callback:origin,doc.to<JsonObject>(),error);
  if(!issued){doc.clear();doc["ok"]=false;doc["error"]["code"]=error;}
  serialize_and_send_json(issued?200:error=="maintenance_request_invalid"?400:503,doc);
}
void admin_exchange() {
  add_response_header("Cache-Control","no-store");
  add_response_header("Referrer-Policy","no-referrer");
  auto input=make_http_json_document();
  if(request_body_size()>4608||!parse_json_body(input)||!input.is<JsonObjectConst>()||
      !input["protocol"].is<int>()||input["protocol"].as<int>()!=2||
      !input["state"].is<const char *>()||!input["token"].is<const char *>()) {
    send_json_response(400,R"({"ok":false,"error":{"code":"maintenance_request_invalid"}})");return;
  }
  for(auto field:input.as<JsonObjectConst>())if(field.key()!="protocol"&&field.key()!="state"&&field.key()!="token") {
    send_json_response(400,R"({"ok":false,"error":{"code":"maintenance_request_invalid"}})");return;
  }
  const auto token=input["token"].as<JsonString>(),state=input["state"].as<JsonString>();
  if(token.size()!=std::char_traits<char>::length(token.c_str())||state.size()!=std::char_traits<char>::length(state.c_str())){
    send_json_response(400,R"({"ok":false,"error":{"code":"maintenance_request_invalid"}})");return;
  }
  auto output=make_http_json_document();std::string error;
  const bool accepted=device_api_maintenance_exchange(std::string(token.c_str(),token.size()),std::string(state.c_str(),state.size()),output.to<JsonObject>(),error);
  if(!accepted){output.clear();output["ok"]=false;output["error"]["code"]=error;}
  serialize_and_send_json(accepted?200:error=="maintenance_trust_loading"?503:403,output);
}
void admin_revoke() {
  add_response_header("Cache-Control","no-store");
  device_api_maintenance_session_revoke(request_header("X-Lunar-Debug-Token").c_str());
  send_json_response(200,R"({"ok":true})");
}

void restore_configuration() {
  add_response_header("Cache-Control", "no-store");
  // Never equate provisioning, an owner ticket or a serial debug token with
  // permission to replace a damaged primary identity/configuration record.
  if (!device_api_admin_access_authorized(request_header("X-Lunar-Debug-Token").c_str())) {
    send_json_response(403,R"({"ok":false,"error":{"code":"administrator_required"}})"); return;
  }
  std::string error;
  const bool restored = restore_configuration_from_recovery(error);
  auto doc = make_http_json_document(); doc["ok"] = restored;
  if (!restored) doc["error"]["code"] = error;
  serialize_and_send_json(restored ? 200 : 503,doc);
}
void repair_recovery_state() {
  add_response_header("Cache-Control", "no-store");
  // Journal repair is part of local OTA maintenance. The OTA owner still
  // requires the recovery role/layout and refuses active writes.
  std::string error;
  const bool repaired = ota_service_repair_recovery_state(error);
  auto doc = make_http_json_document(); doc["ok"] = repaired;
  if (!repaired) doc["error"]["code"] = error;
  serialize_and_send_json(repaired ? 200 : 503,doc);
}
}
#endif
void setup_device_web_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry) {
#if !CONFIG_LUNAREDU_DEVICE_WEB
  (void)registry;
#endif
#if CONFIG_LUNAREDU_DEVICE_WEB
  device_web::freeze();
  StaticPageOptions options;
  options.route = "/";
  options.not_found_redirect_location = "http://192.168.4.1/setup";
  options.read_page_view = read_root;
  options.redirect_not_found = device_api_provisioning_surface_active;
  setup_static_page(options);
  register_http_route("/setup", HTTP_GET, [] { serve(Page::Network); });
  register_http_route("/debug", HTTP_GET, [] { serve(Page::Network, true); });
  register_http_route("/app", HTTP_GET, [] { serve(Page::Application); });
  if (device_web_bound_factory_page_allowed("/factory"))
    register_http_route("/factory", HTTP_GET, [] { serve_asset(device_web::DeviceWebAsset::FactoryPage, "text/html; charset=utf-8"); });
  if (device_web::has_page_override(Page::Binding))
    register_http_route("/binding", HTTP_GET, [] { serve(Page::Binding); });
  // The combined standard server page never replaces a product's custom
  // binding/maintenance safety workflows, which keep their explicit routes.
  register_http_route("/server", HTTP_GET, [] { serve(Page::Network, true); });
  register_http_route("/version", HTTP_GET, [] { serve(Page::Network, true); });
  if (device_web::has_page_override(Page::Maintenance))
    register_http_route("/maintenance", HTTP_GET, [] { serve(Page::Maintenance); });
  register_http_route("/recovery", HTTP_GET, [] { serve(Page::Network, true); });
  register_http_route("/portal.css", HTTP_GET, [] { serve_asset(device_web::DeviceWebAsset::Style, "text/css; charset=utf-8"); });
  register_http_route("/application.css", HTTP_GET, [] { serve_asset(device_web::DeviceWebAsset::ApplicationStyle, "text/css; charset=utf-8"); });
  register_http_route("/device-client.js", HTTP_GET, [] { serve_asset(device_web::DeviceWebAsset::Client, "application/javascript; charset=utf-8"); });
  register_http_route("/portal.js", HTTP_GET, [] { serve_asset(device_web::DeviceWebAsset::Script, "application/javascript; charset=utf-8"); });
  register_http_route("/device-web/config", HTTP_GET, [&registry] { config(registry); });
  register_http_route("/device-web/admin/request", HTTP_GET, admin_request);
  register_http_route("/device-web/admin/exchange", HTTP_POST, admin_exchange);
  register_http_route("/device-web/admin/revoke", HTTP_POST, admin_revoke);
  register_http_route("/device-web/configuration/restore", HTTP_POST, restore_configuration);
  register_http_route("/device-web/recovery/repair", HTTP_POST, repair_recovery_state);
#endif
}
