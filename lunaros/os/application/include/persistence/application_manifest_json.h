#pragma once
#include "app/package/application_package.h"
#include <ArduinoJson.h>

namespace lunaredu::lunaros::apps {
// JSON ingress for Store release documents and persisted package manifests.
bool decode_application_manifest(JsonVariantConst, Application &, std::string &error);
} // namespace lunaredu::lunaros::apps
