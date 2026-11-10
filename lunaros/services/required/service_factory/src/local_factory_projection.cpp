#include "device_api/device_operation_builders.h"
#include "lunaros_factory_projection.h"
#include "projection_lifecycle.h"

#include <iterator>

#include "lunaros_factory.h"
#include "device_api/device_projection.h"

namespace lunaredu::lunaros {
namespace {
DeviceProjectionBinding g_projection;
const DeviceCapabilityDescriptor kCapability{"factory.mode"};

void fill_status(DeviceApiContext &, JsonObject out) {
  const auto view = local_factory_mode().status();
  out["active"] = view.active;
  out["entry"] = view.entry == FactoryEntry::Web ? "web" : "usb";
  out["revision"] = view.revision;
  out["count"] = view.count;
}

void fill_check(JsonObject item, const FactoryCheckSnapshot &check) {
  item["id"] = check.id;
  item["owner"] = check.owner;
  item["owner_label"] = check.owner_label == nullptr ? check.owner : check.owner_label;
  item["label"] = check.label;
  item["kind"] = factory_check_kind_name(check.kind);
  item["state"] = factory_check_state_name(check.state);
  item["detail"] = check.detail;
  item["run_count"] = check.run_count;
}

void fill_catalog(DeviceApiContext &, JsonObject out) {
  const auto view = local_factory_mode().snapshot();
  out["active"] = view.active;
  out["revision"] = view.revision;
  auto items = out["checks"].to<JsonArray>();
  for (size_t i = 0; i < view.count; ++i)
    fill_check(items.add<JsonObject>(), view.checks[i]);
}

void fill_update(JsonObject out, const char *id) {
  FactoryCheckSnapshot check;
  if (local_factory_mode().check(id, check))
    fill_check(out["check"].to<JsonObject>(), check);
  out["revision"] = local_factory_mode().status().revision;
}

DeviceOperationResult operation_result(bool ok, const std::string &error) {
  return ok ? device_operation_ack() :
      device_operation_error(error.c_str(), 409, "factory_mode_rejected");
}

bool web_owns_session() {
  const auto status = local_factory_mode().status();
  return !status.active || status.entry == FactoryEntry::Web;
}

DeviceOperationResult enter(DeviceApiContext &, JsonVariantConst, JsonObject out) {
  std::string error;
  const bool ok = local_factory_mode().enter(FactoryEntry::Web, error);
  if (ok) out["active"] = true;
  return operation_result(ok, error);
}
DeviceOperationResult leave(DeviceApiContext &, JsonVariantConst, JsonObject out) {
  if (!web_owns_session()) return operation_result(false, "factory_mode_owned_by_usb");
  local_factory_mode().leave();
  out["active"] = false;
  return device_operation_ack();
}
DeviceOperationResult start(DeviceApiContext &, JsonVariantConst request, JsonObject out) {
  if (!web_owns_session()) return operation_result(false, "factory_mode_owned_by_usb");
  std::string error;
  const bool ok = local_factory_mode().start(request["id"].as<const char *>(),
      request["action"].as<const char *>(), error);
  if (ok) fill_update(out, request["id"].as<const char *>());
  return operation_result(ok, error);
}
DeviceOperationResult confirm(DeviceApiContext &, JsonVariantConst request, JsonObject out) {
  if (!web_owns_session()) return operation_result(false, "factory_mode_owned_by_usb");
  std::string error;
  const bool ok = local_factory_mode().confirm(request["id"].as<const char *>(),
      request["passed"].as<bool>(), error);
  if (ok) fill_update(out, request["id"].as<const char *>());
  return operation_result(ok, error);
}
DeviceOperationResult reset(DeviceApiContext &, JsonVariantConst request, JsonObject out) {
  if (!web_owns_session()) return operation_result(false, "factory_mode_owned_by_usb");
  std::string error;
  const bool ok = local_factory_mode().reset(request["id"].as<const char *>(), error);
  if (ok) fill_update(out, request["id"].as<const char *>());
  return operation_result(ok, error);
}

constexpr const char *kEmptySchema = R"({"type":"object","additionalProperties":false})";
constexpr const char *kIdSchema = R"({"type":"object","required":["id"],"properties":{"id":{"type":"string"},"action":{"type":"string"}},"additionalProperties":false})";
constexpr const char *kConfirmSchema = R"({"type":"object","required":["id","passed"],"properties":{"id":{"type":"string"},"passed":{"type":"boolean"}},"additionalProperties":false})";
constexpr const char *kAckSchema = R"({"type":"object"})";
constexpr DeviceOperationDefinition kOperations[] = {
    device_operation_schemas(make_device_json_provider("status.get", fill_status), kEmptySchema, kAckSchema),
    device_operation_schemas(make_device_json_provider("catalog.get", fill_catalog), kEmptySchema, kAckSchema),
    device_operation_schemas(make_device_json_command("enter", enter), kEmptySchema, kAckSchema),
    device_operation_schemas(make_device_json_command("exit", leave), kEmptySchema, kAckSchema),
    device_operation_schemas(make_device_json_command("start", start), kIdSchema, kAckSchema),
    device_operation_schemas(make_device_json_command("confirm", confirm), kConfirmSchema, kAckSchema),
    device_operation_schemas(make_device_json_command("reset", reset), kIdSchema, kAckSchema),
};
const DeviceModuleProjectionMetadata kModule{"factory_mode", "factory_mode", nullptr,
                                             "LunarOS local factory checks"};
const DeviceModuleProjection kModuleProjection{"factory.mode", &kModule,
                                               kOperations, std::size(kOperations)};
}  // namespace

bool declare_local_factory_projection(DeviceProjectionRegistry &registry,
                                      std::string &error) {
  bind_factory_projection_lifecycle([](bool available){
    if(!available){g_projection.deactivate(kCapability.id);return true;}
    return g_projection.activate(kCapability);
  });
  return g_projection.declare(registry,
      {&kCapability, &kModuleProjection, nullptr, true}, error);
}

}  // namespace lunaredu::lunaros
