#include "device_api/adapters/application_projection.h"
#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "app/lifecycle/app_controller.h"
#include "app/codec/app_capability_json.h"
#include "lunaredu_pet_device_projection.h"
#include "lunaredu_pet_catalog.h"
#include "projection_storage.h"
#include "device_api/presentation_telemetry.h"
#include "device_api/character_project_projection.h"
#include "lunaredu_pet_desktop_surface.h"

#include <array>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#include "device_api/device_projection.h"
#include "lunaredu_pet_service.h"
#include "lunaredu_pet_feature_preferences.h"
#include "lunaredu_pet_ui.h"
#include "lunaredu_pet_render.h"
#include "lunaredu_pet_types.h"

namespace lunaredu::pet {

using namespace lunaredu::lunaros;
namespace {

constexpr const char *kModuleId = "desktop_pet";
PetDeviceProjectionConfig g_config;
constexpr size_t kOperationCapacity=internal::kProjectionOperationCapacity;
// Registry entries borrow this table for the registered projection's lifetime.
// It is created once in task context, retains a stable address and never falls
// back from external OOM to scarce internal SRAM on a PSRAM-capable target.
internal::ProjectionOperationsOwner g_operations;
bool ensure_operation_storage() {
  return internal::ensure_projection_operations(g_operations);
}
size_t g_operation_count = 0;
DeviceModuleProjection g_surface;

app::CapabilityRegistry *capabilities(DeviceApiContext &device) {
  auto *application = device_api_application_capabilities(device);
  return application;
}

void fill_pet_device_state(JsonObject payload) {
  const auto pet_state = snapshot();
  payload["mood"] = pet_mood_name(pet_state.pet.mood);
  payload["conversation_phase"] = pet_conversation_phase_name(pet_state.conversation_phase);
  payload["boot_progress"] = pet_state.boot_progress;
}

void fill_state(DeviceApiContext &device, JsonObject payload) {
  if (g_config.contribute_product_state) g_config.contribute_product_state(payload);
  if(g_config.observe_desktop_scene)internal::write_desktop_scene_performance(payload,g_config.observe_desktop_scene());
  if (g_config.contribute_performance_state) g_config.contribute_performance_state(payload);
  fill_pet_device_state(payload);
  if (g_config.motion_supported && g_config.contribute_motion_state)
    g_config.contribute_motion_state(device, payload);
  if (g_config.contribute_extension_state) g_config.contribute_extension_state(payload);
}

void fill_features(JsonObject out){const auto p=feature_preferences();out["flexible"]=p.flexible;out["imu"]=p.imu;out["voice"]=p.voice;out["agent"]=p.agent;out["override_remaining_ms"]=diagnostic_features_remaining_ms();}
DeviceOperationResult features_get(DeviceApiContext &,JsonObject out){fill_features(out);return device_operation_ack();}
DeviceOperationResult benchmark_features(DeviceApiContext &,JsonVariantConst request,JsonObject out){
 if(!current_expression())return device_operation_error("pet_not_foreground",409,"pet_benchmark_failed");
 const auto duration=request["duration_ms"];
 if(!duration.is<uint32_t>())return device_operation_error("pet_diagnostic_duration_invalid",400,"pet_benchmark_failed");
 std::string error;
 for(const char *key:{"flexible","imu","voice","agent"})if(!request[key].isNull()&&!request[key].is<bool>())return device_operation_error("pet_feature_boolean_required",400,"pet_benchmark_failed");
 const FeaturePreferences features{request["flexible"]|false,request["imu"]|false,request["voice"]|true,request["agent"]|false};
 if(!request["expression"].isNull()&&(!request["expression"].is<const char *>()||
    !catalog::character_catalog_contains(catalog::kCharacterStates,request["expression"].as<const char *>())))
    return device_operation_error("pet_diagnostic_expression_invalid",400,"pet_benchmark_failed");
 if(!set_diagnostic_features(features,duration.as<uint32_t>(),error))return device_operation_error(error.c_str(),400,"pet_benchmark_failed");
 if(request["expression"].is<const char *>())(void)set_diagnostic_expression(request["expression"].as<const char *>(),duration.as<uint32_t>(),error);
 else reset_diagnostic_expression();
 if(ui_snapshot().mode!=DisplayMode::Fullscreen)(void)ui_action(UiAction::ToggleMode);
 fill_features(out);out["profile"]=features.bits()==4?"pet-rigid-fullscreen-voice-v1":"pet-feature-isolation-v1";return device_operation_ack();
}
DeviceOperationResult benchmark_reset(DeviceApiContext &,JsonVariantConst,JsonObject out){reset_diagnostic_expression();reset_diagnostic_features();fill_features(out);return device_operation_ack();}
DeviceOperationResult state_get(DeviceApiContext &device, JsonObject payload) {
  fill_state(device, payload);
  return device_operation_ack();
}

DeviceOperationResult forward_character_capability(DeviceApiContext &device,
                                                   const char *capability_id,
                                                   uint32_t contract_version,
                                                   JsonVariantConst request,
                                                   JsonObject response,
                                                   bool allow_changes) {
  auto *catalog = capabilities(device);
  if (catalog == nullptr) {
    return device_operation_error("application_catalog_unavailable", 503,
                                  "desktop_pet_error");
  }
  JsonDocument empty_arguments;
  empty_arguments.to<JsonObject>();
  const JsonObjectConst arguments = request.is<JsonObjectConst>()
      ? request.as<JsonObjectConst>()
      : empty_arguments.as<JsonObjectConst>();
  JsonDocument result;
  std::string error;
  if (!app::call_json_capability(*catalog, "device_api", capability_id,
                                 contract_version,
                                 arguments, result.to<JsonObject>(), error,
                                 allow_changes)) {
    return device_operation_error(
        error.empty() ? "character_capability_rejected" : error.c_str(),
        allow_changes ? 409 : 503, "desktop_pet_error");
  }
  response.set(result.as<JsonObjectConst>());
  return device_operation_ack();
}

DeviceOperationResult character_state_get(DeviceApiContext &device,
                                          JsonObject response) {
  JsonDocument request;
  return forward_character_capability(device, "pet.state.get", 1,
                                      request.to<JsonObject>(), response, false);
}

DeviceOperationResult character_catalog_get(DeviceApiContext &device,
                                            JsonObject response) {
  JsonDocument request;
  return forward_character_capability(device, "pet.appearance.catalog", 2,
                                      request.to<JsonObject>(), response, false);
}

DeviceOperationResult character_set(DeviceApiContext &device,
                                    JsonVariantConst request,
                                    JsonObject response) {
  return forward_character_capability(device, "pet.appearance.set", 1, request,
                                      response, true);
}

DeviceOperationResult forward_pet_command(DeviceApiContext &device, const char *id,
                                          JsonVariantConst request,
                                          JsonObject response) {
  size_t count = 0;
  const auto *operations = g_config.performance_operations;
  count = g_config.performance_operation_count;
  for (size_t i = 0; i < count; ++i) {
    if (std::string(operations[i].model.id) != id) continue;
    const auto result = operations[i].projection.json_command_handler(
        device, request, response);
    if (response["state"].is<JsonObject>())
      fill_state(device, response["state"].as<JsonObject>());
    return result;
  }
  return device_operation_error("pet_operation_unavailable", 503, "desktop_pet_error");
}
DeviceOperationResult play_activity(DeviceApiContext &device,
                                    JsonVariantConst request,
                                    JsonObject response) {
  return forward_pet_command(device, "performance.play", request, response);
}
DeviceOperationResult preview_activity(DeviceApiContext &device,
                                       JsonVariantConst request,
                                       JsonObject response) {
  return forward_pet_command(device, "performance.preview", request, response);
}

DeviceOperationResult stop_motion(DeviceApiContext &device, JsonVariantConst,
                                  JsonObject response) {
  std::string response_text;
  std::string error_text;
  auto *catalog = capabilities(device);
  if (catalog == nullptr)
    return device_operation_error("application_catalog_unavailable", 503,
                                  "desktop_pet_error");
  if (!g_config.stop_all_motion(*catalog, response_text, error_text)) {
    return device_operation_error(
        error_text.empty() ? "stop_rejected" : error_text.c_str(), 409,
        "desktop_pet_error");
  }

  response["queued"] = true;
  response["message"] = response_text;
  fill_state(device, response["state"].to<JsonObject>());
  return device_operation_ack();
}

DeviceOperationResult display_intent(DeviceApiContext &device,
                                     const char *intent, const char *failure,
                                     const char *message,
                                     JsonObject response) {
  if (g_config.submit_display_intent == nullptr ||
      !g_config.submit_display_intent(intent)) {
    return device_operation_error(failure, 409, "desktop_pet_error");
  }
  response["queued"] = true;
  response["message"] = message;
  fill_state(device, response["state"].to<JsonObject>());
  return device_operation_ack(202,::lunaredu::lunaros::OperationCompletion::Accepted);
}

DeviceOperationResult character_fullscreen(DeviceApiContext &device, JsonVariantConst,
                                            JsonObject response) {
  return display_intent(device, "pet.character.fullscreen",
                        "character_fullscreen_rejected",
                        "宠物全屏已排队", response);
}

void fill_contract(JsonObject contract) {
  contract["description"] =
      g_config.description != nullptr
          ? g_config.description
          : "Smart pet movement and performance status";
  if (g_config.product_variant != nullptr) {
    contract["product_variant"] = g_config.product_variant;
  }
  if (g_config.supported_targets != nullptr &&
      g_config.supported_target_count > 0) {
    JsonArray targets = contract["supported_targets"].to<JsonArray>();
    for (size_t index = 0; index < g_config.supported_target_count; ++index) {
      if (g_config.supported_targets[index] != nullptr) {
        targets.add(g_config.supported_targets[index]);
      }
    }
  }
  JsonObject features = contract["features"].to<JsonObject>();
  features["character"] = true;
  features["motion"] = g_config.motion_supported;
  features["servo"] = g_config.servo_supported;
  features["touch"] = g_config.touch_supported;
  JsonObject limits = contract["limits"].to<JsonObject>();
  limits["max_speed"] = g_config.max_speed;
  limits["max_duration_ms"] = g_config.max_duration_ms;
  limits["teleop_heartbeat_ms"] = 200;
  limits["teleop_valid_for_ms"] = 600;
  JsonObject commands = contract["commands"].to<JsonObject>();
  commands["desktop_pet.state.get"] =
      g_config.submit_display_intent != nullptr
          ? "read chassis, head, touch and expression state"
          : "read chassis, head and behavior state";
  commands["desktop_pet.character.state.get"] =
      "read the actual character presentation and appearance state";
  commands["desktop_pet.character.catalog.get"] =
      "read all lifecycle, reaction, agent morph and product lifecycle states";
  commands["desktop_pet.character.set"] =
      "set a character state, action, shape, color or appearance mode";
  if (g_config.contribute_motion_contract) g_config.contribute_motion_contract(contract);
  if (g_config.contribute_performance_contract) g_config.contribute_performance_contract(contract);
  if (g_config.submit_display_intent != nullptr) {
    commands["desktop_pet.character.fullscreen"] =
        "open the pet application with the full-screen character";
  }
  if (g_config.stop_all_motion != nullptr)
    commands["desktop_pet.stop"] = "immediate chassis stop";
  if (g_config.contribute_extension_contract)
    g_config.contribute_extension_contract(contract);

}

const DeviceModuleProjectionMetadata kModule = {
    kModuleId,
    kModuleId,
    fill_contract,
};

const DeviceCapabilityDescriptor kPetCapability = {
    kModuleId,
    nullptr,
    0,
};

void fill_status(DeviceApiContext &device, JsonObject payload) {
  fill_state(device, payload);
}

} // namespace

void fill_composed_device_state(DeviceApiContext &device, JsonObject payload) {
  fill_state(device, payload);
}

bool register_pet_device_projection(DeviceProjectionRegistry &registry,
                                    const PetDeviceProjectionConfig &config,
                                    std::string &error) {
  g_config = config;
  static constexpr DeviceMetadataProjection kPetMetadata = {
      "desktop.pet", kModuleId, fill_status, nullptr};
  size_t project_operation_count=0;
  const auto *project_operations=character_project_operations(project_operation_count);
  if (config.additional_operation_count > 8 ||
      (config.additional_operation_count && !config.additional_operations) ||
      (config.motion_operation_count && !config.motion_operations) ||
      (config.performance_operation_count && !config.performance_operations) ||
      7 + project_operation_count + (config.motion_supported ? config.motion_operation_count : 0) +
          config.additional_operation_count +
          (config.submit_display_intent ? 1 : 0) +
          config.performance_operation_count +
          (config.performance_operation_count ? 1 : 0) +
          (config.stop_all_motion ? 1 : 0) > kOperationCapacity ||
      (config.motion_supported && !config.stop_all_motion)) {
    error = "invalid_pet_projection_extension";
    return false;
  }
  if(!ensure_operation_storage()) {
    error="pet_projection_storage_allocation_failed";return false;
  }
  g_operation_count = 0;
  const auto append = [](const DeviceOperationDefinition &definition) {
    (*g_operations)[g_operation_count++] = definition;
  };
  append(device_operation_diagnostic_access(make_device_json_query("features.get",features_get),DeviceDiagnosticAccess::Session));
  append(device_operation_diagnostic_access(make_device_json_command("features.benchmark",benchmark_features),DeviceDiagnosticAccess::Session));
  append(device_operation_diagnostic_access(make_device_json_command("features.benchmark.reset",benchmark_reset),DeviceDiagnosticAccess::Session));
  append(device_operation_diagnostic_access(
      make_device_json_query("state.get", state_get),
      DeviceDiagnosticAccess::Session));
  append(device_operation_remote(make_device_json_query("character.state.get", character_state_get), DeviceRemoteAccess::Query));
  append(device_operation_remote(make_device_json_query("character.catalog.get", character_catalog_get),
                                 DeviceRemoteAccess::Query));
  append(device_operation_remote(make_device_json_command("character.set", character_set),
                                 DeviceRemoteAccess::Command));
  for(size_t index=0;index<project_operation_count;++index)append(project_operations[index]);
  if (config.motion_supported) {
    for (size_t index = 0; index < config.motion_operation_count; ++index)
      append(config.motion_operations[index]);
  }
  for (size_t index = 0; index < config.additional_operation_count; ++index)
    append(config.additional_operations[index]);
  if (config.submit_display_intent != nullptr) {
    append(device_operation_remote(make_device_json_command("character.fullscreen",
                                    character_fullscreen), DeviceRemoteAccess::Command));
  }
  for (size_t i = 0; i < config.performance_operation_count; ++i) {
    const auto &operation = config.performance_operations[i];
    if (std::string(operation.model.id) == "performance.stop") {
      if (config.stop_all_motion)
        append(device_operation_remote(make_device_json_command("performance.stop", stop_motion),
                                       DeviceRemoteAccess::Command));
    } else if (std::string(operation.model.id) == "performance.play") {
      auto decorated = operation;
      decorated.projection.json_command_handler = play_activity;
      append(decorated);
    } else if (std::string(operation.model.id) == "performance.preview") {
      auto decorated = operation;
      decorated.projection.json_command_handler = preview_activity;
      append(decorated);
    } else append(operation);
  }
  if (config.performance_operation_count)
    append(device_operation_remote(make_device_json_query("performance.status.get", state_get),
                                   DeviceRemoteAccess::Query));
  if (config.stop_all_motion)
    append(make_device_json_command("stop", stop_motion));
  g_surface = {kModuleId, &kModule, g_operations->data(), g_operation_count};
  return registry.register_projection(
      {&kPetCapability, &g_surface, &kPetMetadata, false}, &error);
}

bool set_pet_device_projection_available(DeviceRuntimeContext &model,
                                         bool available, std::string &error) {
  error.clear();
  if (!available) {
    (void)model.capabilities.deactivate(kPetCapability.id);
    return true;
  }
  return model.capabilities.activate(kPetCapability, &error);
}

} // namespace lunaredu::pet
