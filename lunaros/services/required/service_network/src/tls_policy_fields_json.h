#pragma once
#include "lunaros_network_tls.h"
#include <ArduinoJson.h>
namespace lunaredu::lunaros::internal {
// Service-local wire codec shared by storage and Device API projections.
// Stored records and command payloads use the same typed, exact-byte fields.
// Semantic/CA validation stays in the policy owner, outside its view lock.
inline bool read_tls_policy_fields(JsonVariantConst fields, NetworkTlsPolicy &policy) {
  if (!fields["origin"].is<const char *>() || !fields["mode"].is<const char *>() ||
      (!fields["ca_pem"].isNull() && !fields["ca_pem"].is<const char *>())) return false;
  const auto text = [](JsonVariantConst field) {
    const auto value = field.as<JsonString>();
    return value.isNull() ? std::string() : std::string(value.c_str(), value.size());
  };
  policy.origin = text(fields["origin"]);
  policy.mode = text(fields["mode"]);
  policy.ca_pem = text(fields["ca_pem"]);
  return true;
}
}
