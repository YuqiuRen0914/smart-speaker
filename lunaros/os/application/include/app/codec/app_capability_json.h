#pragma once

// Device API codec adapter. The native app capability registry has no JSON
// dependency; applications opt into this adapter for external discovery/calls.

#include <ArduinoJson.h>
#include <limits>
#include "app/lifecycle/app_capability.h"

namespace lunaredu::app {

enum class CapabilityExecution : uint8_t { Query, Command, Activity };
enum class CapabilityFieldType : uint8_t { String, Boolean, Integer, Number, Object, Array };
struct CapabilitySchema;

// A deliberately bounded schema vocabulary. The same declaration generates
// JSON Schema for discovery and validates calls; unsupported shapes cannot be
// advertised without implementing their validator. Objects are always closed.
struct CapabilityField {
  const char *name = nullptr;
  CapabilityFieldType type = CapabilityFieldType::String;
  bool required = true;
  const char *description = "";
  double minimum = -std::numeric_limits<double>::infinity();
  double maximum = std::numeric_limits<double>::infinity();
  size_t min_length = 0;
  size_t max_length = 1024;
  const char *const *choices = nullptr;
  size_t choice_count = 0;
  const CapabilitySchema *object_schema = nullptr;
  const CapabilityField *items = nullptr;
  size_t max_items = 32;
};

struct CapabilitySchema {
  const CapabilityField *fields = nullptr;
  size_t count = 0;
};

constexpr CapabilityField object_field(const char *name, const CapabilitySchema &schema, bool required = true) {
  CapabilityField field{name, CapabilityFieldType::Object, required};
  field.object_schema = &schema;
  return field;
}
constexpr CapabilityField array_field(const char *name, const CapabilityField &item, size_t maximum = 32) {
  CapabilityField field{name, CapabilityFieldType::Array};
  field.items = &item;
  field.max_items = maximum;
  return field;
}

using JsonCapabilityHandler = bool (*)(CapabilityRegistry &, void *, const char *caller,
                                      JsonObjectConst arguments, JsonObject result,
                                      std::string &error);
using JsonCapabilityReadiness = bool (*)(CapabilityRegistry &, void *, std::string &reason);

// Declaration, strings, fields and context must outlive the registry. Register
// during product declaration even when its underlying device/data is unavailable.
// Identical declarations may be repeated by service startup/restart after sealing.
struct JsonCapabilityDefinition {
  const char *id = nullptr;
  const char *provider_app = nullptr;
  uint32_t version = 1;
  const char *title = nullptr;
  const char *description = nullptr;
  CapabilityExecution execution = CapabilityExecution::Query;
  CapabilitySchema input;
  CapabilitySchema output;
  JsonCapabilityHandler invoke = nullptr;
  JsonCapabilityReadiness ready = nullptr;
  void *context = nullptr;
  const char *const *allowed_callers = nullptr;
  size_t allowed_caller_count = 0;
  lunaredu::lunaros::OperationExecutor executor=lunaredu::lunaros::OperationExecutor::Main;
};

inline constexpr size_t kCapabilityJsonMaxBytes = 8192;
inline constexpr size_t kCapabilitySchemaMaxFields = 32;
inline constexpr size_t kCapabilityCatalogPageSize = 8;

bool register_json_capability(CapabilityRegistry &catalog, const JsonCapabilityDefinition &, std::string &error);
// Only explicit JSON declarations visible to caller are returned. Availability
// is checked live; an unavailable declaration remains discoverable with reason.
bool list_json_capabilities(CapabilityRegistry &catalog, const char *caller, const char *provider, size_t offset,
                            size_t limit, JsonObject result, std::string &error, bool allow_changes = true);
bool describe_json_capability(CapabilityRegistry &catalog, const char *caller, const char *id,
                              JsonObject result, std::string &error, bool allow_changes = true);
// Caller and permission are supplied by trusted host code, never model JSON.
// Queries remain usable when the foreground policy forbids device changes.
bool call_json_capability(CapabilityRegistry &catalog, const char *caller, const char *id, uint32_t version,
                          JsonObjectConst arguments, JsonObject result,
                          std::string &error, bool allow_changes = true,
                          lunaredu::lunaros::OperationOutcome *outcome=nullptr);

} // namespace lunaredu::app
