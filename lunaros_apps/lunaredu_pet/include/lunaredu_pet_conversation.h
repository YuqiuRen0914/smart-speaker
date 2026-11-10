#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "lunaredu_pet_application_tools.h"

namespace lunaredu::pet::conversation {

enum class StandbyMode : uint8_t {
  WakeWord = 0,
  ContinuousConversation,
};

enum class AssistantEventKind : uint8_t {
  Status,
  Emotion,
  Transcript,
};

// Borrowed event data is valid only for the duration of on_event(). Products
// may project it into Pet behavior, diagnostics, or another presentation.
struct AssistantEvent {
  AssistantEventKind kind = AssistantEventKind::Status;
  const char *value = nullptr;
  const char *detail = nullptr;
  const char *result = nullptr;
  const char *error = nullptr;
};

struct HostCallbacks {
  void *context = nullptr;
  // Optional observer; core Pet event intake is always connected internally.
  void (*on_event)(void *context, const AssistantEvent &event) = nullptr;
  std::string (*last_error)(void *context) = nullptr;
  bool (*request_audio_focus)(void *context, const char *reason,
                              uint32_t timeout_ms) = nullptr;
};

struct ServiceConfig {
  ApplicationToolPolicy tools;
  bool enabled = true;
  HostCallbacks host;
  void *policy_context = nullptr;
  void (*enabled_changed)(void *context, bool enabled) = nullptr;
};

struct ServiceSnapshot {
  bool initialized = false;
  // enabled is the persistent/user intent. suspended is a temporary runtime
  // pause (for example another foreground app owning microphone or speaker).
  bool enabled = false;
  bool suspended = false;
  std::string suspension_reason;
  bool running = false;
  bool activation_ready = false;
  bool activation_pending = false;
  std::string activation_code;
  bool transport_connected = false;
  std::string session_state;
  bool ready = false;
  bool listening = false;
  bool speaking = false;
  bool opening = false;
  bool failed = false;
  std::string last_error;
  std::string last_transport_error;
  bool audio_initialized = false;
  bool audio_input_enabled = false;
  bool audio_output_enabled = false;
  int input_sample_rate = 0;
  int output_sample_rate = 0;
  uint32_t input_failures = 0;
  uint32_t output_failures = 0;
  uint32_t incoming_audio_packets = 0;
  uint32_t downlink_sequence_gaps = 0;
  uint32_t downlink_stale_packets = 0;
  uint32_t downlink_queue_drops = 0;
  uint32_t downlink_decode_failures = 0;
  uint32_t downlink_playback_failures = 0;
  uint32_t output_frames = 0;
  std::string audio_error;
  std::string wake_word;
  std::string wake_word_model;
  std::vector<std::string> available_wake_words;
  bool wake_word_available = false;
  bool wake_word_enabled = false;
  std::string standby_mode;
  std::string transcript_text;
  std::string transcript_role;
  uint32_t transcript_revision = 0;
};

// Bounded, allocation-free state for cooperative realtime loops. Presentation,
// diagnostics and protocol projections continue to use ServiceSnapshot.
struct ServiceRuntimeSnapshot {
  bool initialized = false;
  bool enabled = false;
  bool suspended = false;
  bool activation_ready = false;
  bool activation_pending = false;
  bool transport_connected = false;
  bool ready = false;
  bool listening = false;
  bool thinking = false;
  bool speaking = false;
  bool opening = false;
  bool failed = false;
};

struct ToolSnapshot {
  std::string name;
  std::string description;
  std::string input_schema_json;
};

using ToolCallHandler = bool (*)(void *context, const char *arguments_json,
                                 std::string &response_text,
                                 std::string &error_text);

struct ToolRegistration {
  std::string name;
  std::string description;
  std::string input_schema_json;
  ToolCallHandler handler = nullptr;
  void *context = nullptr;
  // Every external callback executes without the assistant runtime mutex.
};

// Initializes the native service once. FreeRTOS workers remain owned by this
// component; foreground applications only enable or disable the service.
bool setup_service(const ServiceConfig &config = {});
void tick_service();
bool set_enabled(bool enabled);
// Clears the locally persisted XiaoZhi activation/transport credentials and
// blocks automatic activation until request_activation() is called. This is a
// local de-pair operation; deleting the account-side device relationship still
// requires the official XiaoZhi website.
bool clear_activation();
// Explicitly resumes activation after a local de-pair operation.
bool request_activation();
enum class SuspensionReason : uint8_t { AudioFocus = 1, CloudMaintenance = 2 };
// Each owner releases only its own reason; enabled preference is independent.
bool set_suspension(SuspensionReason reason, bool active);
bool begin_cloud_maintenance(uint32_t timeout_ms = 3000);
void end_cloud_maintenance();
bool toggle_listening();
bool finish_listening();
bool set_standby_mode(StandbyMode mode);
bool send_test_message(const char *text = "你好");
ServiceSnapshot snapshot();
ServiceRuntimeSnapshot runtime_snapshot();
std::vector<ToolSnapshot> tool_snapshots();
bool register_tool(const ToolRegistration &tool);

// Declares the stable Device Model/Projection surface before service startup.
bool register_device_projection_surface(std::string &error);

} // namespace lunaredu::pet::conversation
