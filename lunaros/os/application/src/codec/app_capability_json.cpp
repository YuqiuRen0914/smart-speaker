#include "app/codec/app_capability_json.h"

#include <cmath>
#include <cstring>

namespace lunaredu::app {
namespace {
struct Request { JsonObjectConst arguments; };
struct Response { JsonObject result; };

bool fail(std::string &error, const char *message) { error = message; return false; }
bool text(const char *value, size_t maximum) {
  return value && value[0] && std::strlen(value) <= maximum;
}
size_t unicode_length(const char *value, size_t bytes) {
  size_t length = 0;
  for (size_t i = 0; i < bytes;) {
    const auto first = static_cast<unsigned char>(value[i++]);
    if (!first) return SIZE_MAX;
    if (first < 0x80) { ++length; continue; }
    const size_t continuation = first >= 0xC2 && first <= 0xDF ? 1 :
        first >= 0xE0 && first <= 0xEF ? 2 : first >= 0xF0 && first <= 0xF4 ? 3 : 0;
    if (!continuation || i + continuation > bytes) return SIZE_MAX;
    uint32_t point = first & (continuation == 1 ? 0x1F : continuation == 2 ? 0x0F : 0x07);
    for (size_t j = 0; j < continuation; ++j) {
      const auto next = static_cast<unsigned char>(value[i++]);
      if ((next & 0xC0) != 0x80) return SIZE_MAX;
      point = (point << 6) | (next & 0x3F);
    }
    if ((continuation == 1 && point < 0x80) || (continuation == 2 && point < 0x800) ||
        (continuation == 3 && point < 0x10000) || point > 0x10FFFF ||
        (point >= 0xD800 && point <= 0xDFFF)) return SIZE_MAX;
    ++length;
  }
  return length;
}
const char *type_name(CapabilityFieldType type) {
  switch (type) {
  case CapabilityFieldType::String: return "string";
  case CapabilityFieldType::Boolean: return "boolean";
  case CapabilityFieldType::Integer: return "integer";
  case CapabilityFieldType::Number: return "number";
  case CapabilityFieldType::Object: return "object";
  case CapabilityFieldType::Array: return "array";
  }
  return nullptr;
}
const char *execution_name(CapabilityExecution kind) {
  switch (kind) {
  case CapabilityExecution::Query: return "query";
  case CapabilityExecution::Command: return "command";
  case CapabilityExecution::Activity: return "activity";
  }
  return nullptr;
}
bool schema_valid(const CapabilitySchema &schema, size_t depth = 0) {
  if (depth > 4) return false;
  if (schema.count > kCapabilitySchemaMaxFields || (schema.count && !schema.fields)) return false;
  for (size_t i = 0; i < schema.count; ++i) {
    const auto &field = schema.fields[i];
    if (!text(field.name, 64) || !type_name(field.type) || !field.description ||
        std::strlen(field.description) > 256 || std::isnan(field.minimum) ||
        std::isnan(field.maximum) || field.minimum > field.maximum ||
        field.min_length > field.max_length || field.max_length > 1024 ||
        field.choice_count > 32 || (field.choice_count && !field.choices)) return false;
    if (field.choice_count && field.type != CapabilityFieldType::String) return false;
    if (field.type == CapabilityFieldType::Object &&
        (!field.object_schema || !schema_valid(*field.object_schema, depth + 1))) return false;
    if (field.type == CapabilityFieldType::Array &&
        (!field.items || field.max_items > 64 || !schema_valid({field.items, 1}, depth + 1))) return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(field.name, schema.fields[j].name) == 0) return false;
    for (size_t j = 0; j < field.choice_count; ++j) {
      if (!field.choices[j]) return false;
      const size_t length = unicode_length(field.choices[j], std::strlen(field.choices[j]));
      if (length < field.min_length || length > field.max_length) return false;
    }
  }
  return true;
}
const CapabilityField *find_field(const CapabilitySchema &schema, const char *name) {
  for (size_t i = 0; i < schema.count; ++i)
    if (std::strcmp(schema.fields[i].name, name) == 0) return &schema.fields[i];
  return nullptr;
}
bool validate(const CapabilitySchema &, JsonObjectConst, std::string &);
bool matches(const CapabilityField &field, JsonVariantConst value) {
  switch (field.type) {
  case CapabilityFieldType::Object: {
    std::string error;
    return value.is<JsonObjectConst>() && validate(*field.object_schema, value.as<JsonObjectConst>(), error);
  }
  case CapabilityFieldType::Array:
    if (!value.is<JsonArrayConst>() || value.size() > field.max_items) return false;
    for (JsonVariantConst item : value.as<JsonArrayConst>()) if (!matches(*field.items, item)) return false;
    return true;
  case CapabilityFieldType::Boolean: return value.is<bool>();
  case CapabilityFieldType::String: {
    if (!value.is<const char *>()) return false;
    const JsonString string = value.as<JsonString>();
    // JSON Schema string lengths count Unicode characters, not UTF-8 bytes.
    const size_t length = unicode_length(string.c_str(), string.size());
    if (length < field.min_length || length > field.max_length) return false;
    if (!field.choice_count) return true;
    for (size_t i = 0; i < field.choice_count; ++i)
      if (std::strcmp(string.c_str(), field.choices[i]) == 0) return true;
    return false;
  }
  case CapabilityFieldType::Integer:
  case CapabilityFieldType::Number: {
    if (value.is<bool>() || !value.is<double>()) return false;
    const double number = value.as<double>();
    return std::isfinite(number) && number >= field.minimum && number <= field.maximum &&
        (field.type != CapabilityFieldType::Integer || std::trunc(number) == number);
  }
  }
  return false;
}
bool validate(const CapabilitySchema &schema, JsonObjectConst object, std::string &error) {
  if (object.isNull() || measureJson(object) > kCapabilityJsonMaxBytes)
    return fail(error, "capability_object_invalid_or_oversized");
  for (const JsonPairConst member : object) {
    if (member.key().size() != std::strlen(member.key().c_str()))
      return fail(error, "invalid_capability_field_name");
    const auto *field = find_field(schema, member.key().c_str());
    if (!field) { error = std::string("unknown_capability_field:") + member.key().c_str(); return false; }
    if (!matches(*field, member.value())) {
      error = std::string("invalid_capability_field:") + field->name; return false;
    }
  }
  for (size_t i = 0; i < schema.count; ++i) {
    const auto &field = schema.fields[i];
    if (field.required && object[field.name].isNull()) {
      error = std::string("missing_capability_field:") + field.name; return false;
    }
  }
  return true;
}
void write_schema(const CapabilitySchema &, JsonObject);
void write_field(const CapabilityField &field, JsonObject property) {
  property["type"] = type_name(field.type);
  if (field.description[0]) property["description"] = field.description;
  if (field.type == CapabilityFieldType::Object) write_schema(*field.object_schema, property);
  else if (field.type == CapabilityFieldType::Array) {
    property["maxItems"] = field.max_items;
    write_field(*field.items, property["items"].to<JsonObject>());
  } else if (field.type == CapabilityFieldType::String) {
    property["minLength"] = field.min_length;
    property["maxLength"] = field.max_length;
    if (field.choice_count) {
      JsonArray choices = property["enum"].to<JsonArray>();
      for (size_t j = 0; j < field.choice_count; ++j) choices.add(field.choices[j]);
    }
  } else if (field.type == CapabilityFieldType::Integer || field.type == CapabilityFieldType::Number) {
    if (std::isfinite(field.minimum)) property["minimum"] = field.minimum;
    if (std::isfinite(field.maximum)) property["maximum"] = field.maximum;
  }
}
void write_schema(const CapabilitySchema &schema, JsonObject output) {
  output["type"] = "object";
  output["additionalProperties"] = false;
  JsonObject properties = output["properties"].to<JsonObject>();
  JsonArray required = output["required"].to<JsonArray>();
  for (size_t i = 0; i < schema.count; ++i) {
    const auto &field = schema.fields[i];
    write_field(field, properties[field.name].to<JsonObject>());
    if (field.required) required.add(field.name);
  }
}

bool ready_detail(CapabilityRegistry &catalog,void *context,std::string &reason) {
  const auto &definition = *static_cast<const JsonCapabilityDefinition *>(context);
  return !definition.ready || definition.ready(catalog, definition.context, reason);
}
bool ready(CapabilityRegistry &catalog,void *context){std::string reason;return ready_detail(catalog,context,reason);}
bool dispatch(void *context, const CapabilityCall &call, std::string &error) {
  const auto &definition = *static_cast<const JsonCapabilityDefinition *>(context);
  const auto &request = *static_cast<const Request *>(call.request);
  auto &response = *static_cast<Response *>(call.response);
  response.result.clear();
  if (!validate(definition.input, request.arguments, error)) {
    if(call.outcome)*call.outcome={lunaros::OperationCompletion::NotStarted,lunaros::OperationError::InvalidArgument};
    return false;
  }
  if (!definition.invoke(*call.catalog, definition.context, call.caller, request.arguments, response.result, error)) {
    response.result.clear(); return false;
  }
  if (!validate(definition.output, response.result, error)) {
    if(call.outcome)*call.outcome={lunaros::OperationCompletion::Unknown,lunaros::OperationError::Internal};
    error = "capability_result_contract_violation:" + error;
    response.result.clear(); return false;
  }
  if(call.outcome)call.outcome->completion=definition.execution==CapabilityExecution::Activity?
    lunaros::OperationCompletion::Accepted:lunaros::OperationCompletion::Completed;
  return true;
}
const JsonCapabilityDefinition *find(CapabilityRegistry &catalog, const char *caller, const char *id) {
  CapabilityDefinition definition;
  return catalog.find_capability(id, caller, definition) && definition.invoke == dispatch
      ? static_cast<const JsonCapabilityDefinition *>(definition.context) : nullptr;
}
void write_summary(CapabilityRegistry &catalog,const char *caller,const JsonCapabilityDefinition &definition, JsonObject output, bool allow_changes) {
  output["id"] = definition.id;
  output["provider"] = definition.provider_app;
  output["version"] = definition.version;
  output["acknowledgement"]=lunaros::operation_completion_name(definition.execution==CapabilityExecution::Activity?
    lunaros::OperationCompletion::Accepted:lunaros::OperationCompletion::Completed);
  output["title"] = definition.title;
  output["description"] = definition.description;
  output["execution"] = execution_name(definition.execution);
  std::string reason;
  const bool blocked = !allow_changes && definition.execution != CapabilityExecution::Query;
  const bool available = !blocked && catalog.capability_available(definition.id,caller,&reason);
  if (blocked) reason = "application_capability_changes_blocked";
  if (reason.size() > 256) reason.resize(256);
  output["available"] = available;
  if (!available) output["unavailable_reason"] = reason.empty() ? "application_capability_unavailable" : reason;
}
} // namespace

bool register_json_capability(CapabilityRegistry &catalog, const JsonCapabilityDefinition &definition, std::string &error) {
  if (!text(definition.id, 96) || !text(definition.provider_app, 64) ||
      !text(definition.title, 96) || !text(definition.description, 512) ||
      !execution_name(definition.execution) || !definition.invoke ||
      !schema_valid(definition.input) || !schema_valid(definition.output))
    return fail(error, "invalid_json_capability_declaration");
  JsonDocument schemas;
  write_schema(definition.input, schemas["input_schema"].to<JsonObject>());
  write_schema(definition.output, schemas["output_schema"].to<JsonObject>());
  if (schemas.overflowed() || measureJson(schemas) > 14000)
    return fail(error, "capability_schema_too_large");
  return catalog.register_capability({definition.id, definition.provider_app, definition.version,
      sizeof(Request), sizeof(Response), dispatch, ready,
      const_cast<JsonCapabilityDefinition *>(&definition), definition.allowed_callers,
      definition.allowed_caller_count,definition.executor,ready_detail}, error);
}

bool list_json_capabilities(CapabilityRegistry &catalog, const char *caller, const char *provider, size_t offset,
                            size_t limit, JsonObject result, std::string &error, bool allow_changes) {
  result.clear();
  if (!text(caller, 64) || !limit || limit > kCapabilityCatalogPageSize || offset > kCallableCapabilityCapacity)
    return fail(error, "invalid_capability_catalog_request");
  struct Page { CapabilityRegistry &catalog; const char *provider; size_t offset, limit, total = 0, emitted = 0; JsonArray items; bool changes; const char *caller; };
  Page page{catalog, provider, offset, limit, 0, 0, result["capabilities"].to<JsonArray>(), allow_changes,caller};
  catalog.visit_capabilities(caller, [](const CapabilityDefinition &native, bool, void *context) {
    auto &page = *static_cast<Page *>(context);
    const auto *definition = native.invoke == dispatch
        ? static_cast<const JsonCapabilityDefinition *>(native.context) : nullptr;
    if (!definition || (page.provider && page.provider[0] &&
        std::strcmp(page.provider, definition->provider_app) != 0)) return;
    if (page.total++ < page.offset || page.emitted >= page.limit) return;
    write_summary(page.catalog,page.caller,*definition,page.items.add<JsonObject>(),page.changes); ++page.emitted;
  }, &page, 0, false);
  result["total"] = page.total;
  if (offset + page.emitted < page.total) result["next_offset"] = offset + page.emitted;
  error.clear(); return true;
}

bool describe_json_capability(CapabilityRegistry &catalog, const char *caller, const char *id, JsonObject result, std::string &error, bool allow_changes) {
  result.clear();
  const auto *definition = find(catalog, caller, id);
  if (!definition) return fail(error, "application_capability_not_found_or_denied");
  write_summary(catalog,caller,*definition,result,allow_changes);
  write_schema(definition->input, result["input_schema"].to<JsonObject>());
  write_schema(definition->output, result["output_schema"].to<JsonObject>());
  error.clear(); return true;
}

bool call_json_capability(CapabilityRegistry &catalog, const char *caller, const char *id, uint32_t version,
                          JsonObjectConst arguments, JsonObject result,
                          std::string &error, bool allow_changes,lunaros::OperationOutcome *outcome) {
  if(outcome)*outcome={lunaros::OperationCompletion::NotStarted,lunaros::OperationError::Forbidden};
  result.clear();
  const auto *definition = find(catalog, caller, id);
  if (!definition) return fail(error, "application_capability_not_found_or_denied");
  if (!allow_changes && definition->execution != CapabilityExecution::Query)
    return fail(error, "application_capability_changes_blocked");
  Request request{arguments}; Response response{result};
  // The native registry rechecks caller, version, sizes and availability at
  // dispatch time. Discovery does not grant a durable permission or lease.
  return catalog.call_capability(id, caller, version, request, response, error,outcome);
}
} // namespace lunaredu::app
