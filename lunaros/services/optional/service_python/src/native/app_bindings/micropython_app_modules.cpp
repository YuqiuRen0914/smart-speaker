#include "app/codec/app_capability_json.h"
#include "micropython_runtime/micropython_runtime_context.h"
#include "lunaros_micropython_runtime_config.h"
#include "lunaros_micropython_runtime.h"
#include "native/binding_support.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
#include "lunaredu_assistant_conversation.h"
#endif

using namespace lunaredu::micropython;
namespace {
// JSON is an explicit VM boundary; native Apps keep their typed contracts.
// Identity is fixed by this binding and cannot be supplied by the script.
mp_obj_t apps_call(mp_obj_t id_obj, mp_obj_t args_obj) {
  if (micropython_background_active())
    mp_raise_ValueError(MP_ERROR_TEXT("background programs must use lunar.call"));
  const char *id = mp_obj_str_get_str(id_obj);
  size_t bytes = 0;
  const char *json = mp_obj_str_get_data(args_obj, &bytes);
  if (bytes > lunaredu::app::kCapabilityJsonMaxBytes)
    mp_raise_ValueError(MP_ERROR_TEXT("capability arguments too large"));
  JsonDocument arguments;
  NlrCleanup arguments_cleanup(arguments);
  JsonDocument result;
  NlrCleanup result_cleanup(result);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (deserializeJson(arguments, json, bytes) || !arguments.is<JsonObject>())
    mp_raise_ValueError(MP_ERROR_TEXT("capability arguments must be a JSON object"));
  auto *capabilities = lunaredu::micropython::runtime_application_capabilities();
  if (capabilities == nullptr ||
      !lunaredu::app::call_json_capability(*capabilities, "micropython", id, 1,
          arguments.as<JsonObjectConst>(), result.to<JsonObject>(), error))
    raise_operation_failed(error.c_str());
  std::string encoded;
  NlrCleanup encoded_cleanup(encoded);
  if (measureJson(result) > lunaredu::app::kCapabilityJsonMaxBytes)
    mp_raise_ValueError(MP_ERROR_TEXT("capability result too large"));
  serializeJson(result, encoded);
  return mp_obj_new_str(encoded.data(), encoded.size());
}
MP_DEFINE_CONST_FUN_OBJ_2(apps_call_obj, apps_call);

#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
constexpr const char *kCapabilityLunarLink = "lunarlink_assistant";
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
constexpr const char *kCapabilityRobot = "desktop_pet";
const RobotMotionLimits &robot_limits() {
  const auto *port = runtime_robot_motion_port();
  if (!port || !port->limits.valid())
    mp_raise_ValueError(MP_ERROR_TEXT("robot motion policy unavailable"));
  return port->limits;
}
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
mp_obj_t lunarlink_status_dict(
    const lunaredu::assistant::ServiceSnapshot &status) {
  mp_obj_t dict = mp_obj_new_dict(18);
  dict_store(dict, "available",
             py_bool(capability_available(kCapabilityLunarLink)));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "enabled", py_bool(status.enabled));
  dict_store(dict, "activation_ready", py_bool(status.activation_ready));
  dict_store(dict, "activation_pending", py_bool(status.activation_pending));
  dict_store(dict, "activation_code", py_str(status.activation_code.c_str()));
  dict_store(dict, "transport_connected",
             py_bool(status.transport_connected));
  dict_store(dict, "session_state", py_str(status.session_state.c_str()));
  dict_store(dict, "ready", py_bool(status.ready));
  dict_store(dict, "listening", py_bool(status.listening));
  dict_store(dict, "speaking", py_bool(status.speaking));
  dict_store(dict, "opening", py_bool(status.opening));
  dict_store(dict, "failed", py_bool(status.failed));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  dict_store(dict, "audio_initialized", py_bool(status.audio_initialized));
  dict_store(dict, "audio_input_enabled",
             py_bool(status.audio_input_enabled));
  dict_store(dict, "audio_output_enabled",
             py_bool(status.audio_output_enabled));
  dict_store(dict, "audio_error", py_str(status.audio_error.c_str()));
  return dict;
}
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
mp_obj_t robot_status() {
  auto *capabilities = lunaredu::micropython::runtime_application_capabilities();
  if (capabilities == nullptr) {
    raise_operation_failed("robot runtime");
  }
  lunaredu::micropython::RobotMotionStatus status;
  std::string error;
  const auto *port = lunaredu::micropython::runtime_robot_motion_port();
  if (port && port->status && !port->status(*capabilities, status, error))
    raise_operation_failed(error.c_str());
  NlrCleanup status_cleanup(status);
  mp_obj_t dict=mp_obj_new_dict(8);
  dict_store(dict,"available",py_bool(capability_available(kCapabilityRobot)));
  dict_store(dict,"active",py_bool(status.active));
  dict_store(dict,"moving",py_bool(status.moving));
  dict_store(dict,"left_speed",py_float(status.left_speed));
  dict_store(dict,"right_speed",py_float(status.right_speed));
  dict_store(dict,"pan",py_float(status.pan));dict_store(dict,"tilt",py_float(status.tilt));
  dict_store(dict,"max_speed",py_float(robot_limits().maximum_speed));
  dict_store(dict,"max_duration_ms",py_i32(static_cast<int>(robot_limits().maximum_duration_ms)));
  return dict;
}
MP_DEFINE_CONST_FUN_OBJ_0(robot_status_obj, robot_status);

mp_obj_t robot_available() {
  return py_bool(capability_available(kCapabilityRobot));
}
MP_DEFINE_CONST_FUN_OBJ_0(robot_available_obj, robot_available);

float robot_speed(mp_obj_t value) {
  const float parsed = static_cast<float>(mp_obj_get_float(value));
  if (!std::isfinite(parsed)) {
    mp_raise_ValueError(MP_ERROR_TEXT("robot speed must be finite"));
  }
  return std::clamp(parsed, 0.0f, robot_limits().maximum_speed);
}

int robot_duration(mp_obj_t value) {
  return std::clamp(static_cast<int>(mp_obj_get_int(value)),
                    static_cast<int>(robot_limits().minimum_duration_ms), static_cast<int>(robot_limits().maximum_duration_ms));
}

mp_obj_t queue_robot_json(const char *json, const char *fixed_action = nullptr) {
  require_capability(kCapabilityRobot);
  std::string response;
  NlrCleanup response_cleanup(response);
  std::string error;
  NlrCleanup error_cleanup(error);
  auto *capabilities = lunaredu::micropython::runtime_application_capabilities();
  const auto *port = lunaredu::micropython::runtime_robot_motion_port();
  if (capabilities == nullptr || port == nullptr || port->execute == nullptr ||
      !port->execute(*capabilities, json, fixed_action, response, error)) {
    raise_operation_failed(error.empty() ? "robot command" : error.c_str());
  }
  return robot_status();
}

mp_obj_t robot_drive(size_t n_args, const mp_obj_t *args) {
  const float raw_linear = static_cast<float>(mp_obj_get_float(args[0]));
  const float raw_turn = static_cast<float>(mp_obj_get_float(args[1]));
  if (!std::isfinite(raw_linear) || !std::isfinite(raw_turn))
    mp_raise_ValueError(MP_ERROR_TEXT("robot speed must be finite"));
  const float linear = std::clamp(raw_linear,
                                  -robot_limits().maximum_speed, robot_limits().maximum_speed);
  const float turn = std::clamp(raw_turn,
                                -robot_limits().maximum_speed, robot_limits().maximum_speed);
  const int duration = n_args >= 3 ? robot_duration(args[2]) : static_cast<int>(robot_limits().default_duration_ms);
  char json[128];
  std::snprintf(json, sizeof(json),
                "{\"action\":\"drive\",\"linear\":%.4f,\"turn\":%.4f,"
                "\"duration_ms\":%d}",
                static_cast<double>(linear), static_cast<double>(turn),
                duration);
  return queue_robot_json(json);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(robot_drive_obj, 2, 3, robot_drive);

mp_obj_t robot_direction(size_t n_args, const mp_obj_t *args,
                         const char *action) {
  const float speed = n_args >= 1 ? robot_speed(args[0]) : robot_limits().default_speed;
  const int duration = n_args >= 2 ? robot_duration(args[1]) : static_cast<int>(robot_limits().default_duration_ms);
  char json[112];
  std::snprintf(json, sizeof(json),
                "{\"action\":\"%s\",\"speed\":%.4f,"
                "\"duration_ms\":%d}",
                action, static_cast<double>(speed), duration);
  return queue_robot_json(json);
}

mp_obj_t robot_forward(size_t n_args, const mp_obj_t *args) {
  return robot_direction(n_args, args, "forward");
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(robot_forward_obj, 0, 2, robot_forward);

mp_obj_t robot_backward(size_t n_args, const mp_obj_t *args) {
  return robot_direction(n_args, args, "backward");
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(robot_backward_obj, 0, 2, robot_backward);

mp_obj_t robot_turn_left(size_t n_args, const mp_obj_t *args) {
  return robot_direction(n_args, args, "turn_left");
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(robot_turn_left_obj, 0, 2,
                                    robot_turn_left);

mp_obj_t robot_turn_right(size_t n_args, const mp_obj_t *args) {
  return robot_direction(n_args, args, "turn_right");
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(robot_turn_right_obj, 0, 2,
                                    robot_turn_right);

mp_obj_t robot_stop() { return queue_robot_json("{}", "stop"); }
MP_DEFINE_CONST_FUN_OBJ_0(robot_stop_obj, robot_stop);
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
mp_obj_t lunarlink_available() {
  return py_bool(capability_available(kCapabilityLunarLink));
}
MP_DEFINE_CONST_FUN_OBJ_0(lunarlink_available_obj, lunarlink_available);

mp_obj_t lunarlink_status() {
  return nlr_convert(lunaredu::assistant::snapshot(), lunarlink_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(lunarlink_status_obj, lunarlink_status);

mp_obj_t lunarlink_set_enabled(mp_obj_t enabled_obj) {
  require_capability(kCapabilityLunarLink);
  if (!lunaredu::assistant::set_enabled(mp_obj_is_true(enabled_obj))) {
    raise_operation_failed("lunarlink.set_enabled");
  }
  return lunarlink_status();
}
MP_DEFINE_CONST_FUN_OBJ_1(lunarlink_set_enabled_obj, lunarlink_set_enabled);

mp_obj_t lunarlink_enable() { return lunarlink_set_enabled(mp_const_true); }
MP_DEFINE_CONST_FUN_OBJ_0(lunarlink_enable_obj, lunarlink_enable);

mp_obj_t lunarlink_disable() { return lunarlink_set_enabled(mp_const_false); }
MP_DEFINE_CONST_FUN_OBJ_0(lunarlink_disable_obj, lunarlink_disable);

mp_obj_t lunarlink_toggle_listening() {
  require_capability(kCapabilityLunarLink);
  if (!lunaredu::assistant::toggle_listening()) {
    raise_operation_failed("lunarlink.toggle_listening");
  }
  return lunarlink_status();
}
MP_DEFINE_CONST_FUN_OBJ_0(lunarlink_toggle_listening_obj,
                          lunarlink_toggle_listening);

mp_obj_t lunarlink_tools() {
  mp_obj_t list = mp_obj_new_list(0, nullptr);
  const auto tools = lunaredu::assistant::tool_snapshots();
  NlrCleanup tools_cleanup(tools);
  for (const auto &tool : tools) {
    mp_obj_t item = mp_obj_new_dict(3);
    dict_store(item, "name", py_str(tool.name.c_str()));
    dict_store(item, "description", py_str(tool.description.c_str()));
    dict_store(item, "input_schema_json",
               py_str(tool.input_schema_json.c_str()));
    mp_obj_list_append(list, item);
  }
  return list;
}
MP_DEFINE_CONST_FUN_OBJ_0(lunarlink_tools_obj, lunarlink_tools);
#endif

} // namespace
namespace lunaredu::micropython {
void register_app_binding_modules(mp_obj_t root) {
  mp_obj_t apps = make_module("lunaredu.apps");
  module_store(apps, "call", MP_OBJ_FROM_PTR(&apps_call_obj));
  register_submodule(root, "apps", apps);
  (void)root;
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
  const auto *robot_port = runtime_robot_motion_port();
  if (robot_port != nullptr && robot_port->execute != nullptr && robot_port->limits.valid()) {
    mp_obj_t robot = make_module("lunaredu.robot");
    module_store(robot, "available", MP_OBJ_FROM_PTR(&robot_available_obj));
    module_store(robot, "status", MP_OBJ_FROM_PTR(&robot_status_obj));
    module_store(robot, "drive", MP_OBJ_FROM_PTR(&robot_drive_obj));
    module_store(robot, "forward", MP_OBJ_FROM_PTR(&robot_forward_obj));
    module_store(robot, "backward", MP_OBJ_FROM_PTR(&robot_backward_obj));
    module_store(robot, "turn_left", MP_OBJ_FROM_PTR(&robot_turn_left_obj));
    module_store(robot, "turn_right", MP_OBJ_FROM_PTR(&robot_turn_right_obj));
    module_store(robot, "stop", MP_OBJ_FROM_PTR(&robot_stop_obj));
    register_submodule(root, "robot", robot);
  }
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
  mp_obj_t lunarlink = make_module("lunaredu.lunarlink");
  module_store(lunarlink, "available",
               MP_OBJ_FROM_PTR(&lunarlink_available_obj));
  module_store(lunarlink, "status", MP_OBJ_FROM_PTR(&lunarlink_status_obj));
  module_store(lunarlink, "set_enabled",
               MP_OBJ_FROM_PTR(&lunarlink_set_enabled_obj));
  module_store(lunarlink, "enable", MP_OBJ_FROM_PTR(&lunarlink_enable_obj));
  module_store(lunarlink, "disable", MP_OBJ_FROM_PTR(&lunarlink_disable_obj));
  module_store(lunarlink, "toggle_listening",
               MP_OBJ_FROM_PTR(&lunarlink_toggle_listening_obj));
  module_store(lunarlink, "tools", MP_OBJ_FROM_PTR(&lunarlink_tools_obj));
  register_submodule(root, "lunarlink", lunarlink);
#endif
}
void append_app_binding_status(mp_obj_t dict) {
  (void)dict;
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
  dict_store(dict, "lunarlink",
             nlr_convert(lunaredu::assistant::snapshot(), lunarlink_status_dict));
#endif
}
} // namespace lunaredu::micropython
